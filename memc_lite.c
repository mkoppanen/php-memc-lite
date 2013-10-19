/*
   +----------------------------------------------------------------------+
   | PHP Version 5 / memc-lite                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2006-2013 Mikko Koppanen <mikko.koppanen@gmail.com>    |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Mikko Koppanen <mikko.koppanen@gmail.com>                    |
   +----------------------------------------------------------------------+
*/

#include "php_memc_lite.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"
#include "Zend/zend_exceptions.h"
#include <ext/standard/php_smart_str.h>

#include <libmemcached/memcached.h>

#define FASTLZ_FUNCTION_PREFIX memc_lite
#include "fastlz/fastlz.h"

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be64toh(x) betoh64(x)
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define htobe64(x) OSSwapHostToBigInt64(x)
#  define be64toh(x) OSSwapBigToHostInt64(x)
#endif

#include <stdint.h>
#include <stdlib.h>

/* Compatibility with older versions */
#ifdef HAVE_LIBMEMCACHED_INSTANCE_ST
typedef const memcached_instance_st * memc_lite_instance_st;
#else
typedef memcached_server_instance_st memc_lite_instance_st;
#endif


static
	zend_class_entry *php_memc_lite_sc_entry,
					 *php_memc_lite_exception_sc_entry;

static
	zend_object_handlers memc_lite_object_handlers;

static
	int le_memc_lite_internal_st;

typedef enum _php_memc_lite_distribution_t {
	PHP_MEMC_LITE_DISTRIBUTION_MIN,
	PHP_MEMC_LITE_DISTRIBUTION_MODULO,
	PHP_MEMC_LITE_DISTRIBUTION_KETAMA,
	PHP_MEMC_LITE_DISTRIBUTION_MAX
} php_memc_lite_distribution_t;

typedef struct _php_memc_sasl_credentials_t {
	char *username;
	char *password;
} php_memc_sasl_credentials_t;

typedef struct _php_memc_lite_internal_t {
	memcached_st *memc;
	zend_bool is_persistent;
	php_memc_lite_distribution_t distribution;
	zend_bool compression;
	php_memc_sasl_credentials_t sasl_credentials;
} php_memc_lite_internal_t;

typedef struct _php_memc_lite_object  {
	zend_object zo;
	php_memc_lite_internal_t *internal;
} php_memc_lite_object;

typedef enum _php_memc_lite_op_t {
	PHP_MEMC_LITE_OP_INCR  = 1,
	PHP_MEMC_LITE_OP_DECR  = 2,
} php_memc_lite_op_t;

#define MEMC_LITE_FLAG_IS_STRING     (1 << 1)
#define MEMC_LITE_FLAG_IS_LONG       (1 << 2)
#define MEMC_LITE_FLAG_IS_DOUBLE     (1 << 3)
#define MEMC_LITE_FLAG_IS_BOOL       (1 << 4)
#define MEMC_LITE_FLAG_IS_NULL       (1 << 5)
#define MEMC_LITE_FLAG_IS_SERIALIZED (1 << 6)
#define MEMC_LITE_FLAG_IS_COMPRESSED (1 << 7)

#define MEMC_LITE_VERY_SMALL 128

static
zend_bool s_handle_libmemcached_return (memcached_st *memc, const char *name, memcached_return rc TSRMLS_DC)
{
	// No error
	if (rc == MEMCACHED_SUCCESS)
		return 0;

	/* Is this an error..? */
	if (rc == MEMCACHED_END) {
		return 0;
	}

	zend_throw_exception_ex (php_memc_lite_exception_sc_entry, rc TSRMLS_CC, "Call to %s failed: %s", name, memcached_strerror (memc, rc));
	return 1;
}

static
php_memc_lite_internal_t *s_memc_lite_internal_new (zend_bool is_persistent TSRMLS_DC)
{
	php_memc_lite_internal_t *internal;

	internal = pecalloc(1, sizeof (php_memc_lite_internal_t), is_persistent);

	// Create a new memcached_st struct
	internal->memc = memcached_create (NULL);

	if (!internal->memc) {
		zend_error (E_ERROR, "Failed to allocate memory for struct memcached_st");
	}

	internal->compression  = 0;
	internal->distribution = PHP_MEMC_LITE_DISTRIBUTION_MODULO;

	memcached_behavior_set (internal->memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_MODULA);
	memcached_behavior_set (internal->memc, MEMCACHED_BEHAVIOR_HASH, MEMCACHED_HASH_DEFAULT);

	internal->sasl_credentials.username = NULL;
	internal->sasl_credentials.password = NULL;

	internal->is_persistent = is_persistent;
	return internal;
}

static
php_memc_lite_internal_t *s_memc_lite_internal_get (const char *persistent_id, zend_bool *is_new TSRMLS_DC)
{
	zend_bool is_persistent;
	php_memc_lite_internal_t *internal;

	char *plist_key;
	int plist_key_len;
	zend_rsrc_list_entry *le_p = NULL;

	*is_new = 0;
	is_persistent = (persistent_id ? 1 : 0);

	if (is_persistent) {
		plist_key_len = spprintf (&plist_key, 0, "memc_lite:[%s]", persistent_id);

		if (zend_hash_find(&EG(persistent_list), plist_key, plist_key_len, (void *) &le_p) == SUCCESS) {
			if (le_p->type == le_memc_lite_internal_st) {
				efree (plist_key);
				return (php_memc_lite_internal_t *) le_p->ptr;
			}
		}
		efree (plist_key);
	}

	internal = s_memc_lite_internal_new (is_persistent TSRMLS_CC);
	*is_new  = 1;
	return internal;
}

static
zend_bool s_invoke_on_new_object(zval *this_obj, zend_fcall_info *fci, zend_fcall_info_cache *fci_cache, const char *persistent_id TSRMLS_DC)
{
	zval *retval_ptr, *pid_z;
	zval **params[2];
	zend_bool retval = 1;

	ALLOC_INIT_ZVAL(pid_z);

	if (persistent_id) {
		ZVAL_STRING(pid_z, persistent_id, 1);
	} else {
		ZVAL_NULL(pid_z);
	}

	/* Call the cb */
	params[0] = &this_obj;
	params[1] = &pid_z;

	fci->params         = params;
	fci->param_count    = 2;
	fci->retval_ptr_ptr = &retval_ptr;
	fci->no_separation  = 1;

	if (zend_call_function(fci, fci_cache TSRMLS_CC) == FAILURE) {
		if (!EG(exception)) {
			zend_throw_exception_ex (php_memc_lite_exception_sc_entry, 0 TSRMLS_CC, "Failed to invoke 'on_new_obj' callback %s()", Z_STRVAL_P (fci->function_name));
		}
		retval = 0;
	}
	zval_ptr_dtor(&pid_z);

	if (retval_ptr) {
		zval_ptr_dtor(&retval_ptr);
	}

	if (EG(exception)) {
		retval = 0;
	}

	return retval;
}

static
void s_memc_lite_internal_store (const char *persistent_id, php_memc_lite_internal_t *internal TSRMLS_DC)
{
	zend_rsrc_list_entry le;
	char *plist_key;
	int plist_key_len;

	plist_key_len = spprintf (&plist_key, 0, "memc_lite:[%s]", persistent_id);

	le.type = le_memc_lite_internal_st;
	le.ptr  = internal;

	if (zend_hash_update(&EG(persistent_list), (char *)plist_key, plist_key_len, (void *)&le, sizeof(le), NULL) == FAILURE) {
		efree (plist_key);
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Could not register persistent entry for the context");
	}
	efree (plist_key);
}

#if ZEND_MODULE_API_NO > 20060613
#  define PHP_MEMC_LITE_ERROR_HANDLING_INIT() zend_error_handling error_handling;
#  define PHP_MEMC_LITE_ERROR_HANDLING_THROW() zend_replace_error_handling(EH_THROW, php_memc_lite_exception_sc_entry, &error_handling TSRMLS_CC);
#  define PHP_MEMC_LITE_ERROR_HANDLING_RESTORE() zend_restore_error_handling(&error_handling TSRMLS_CC);
#else
#  define PHP_MEMC_LITE_ERROR_HANDLING_INIT()
#  define PHP_MEMC_LITE_ERROR_HANDLING_THROW() php_set_error_handling(EH_THROW, php_memc_lite_exception_sc_entry TSRMLS_CC);
#  define PHP_MEMC_LITE_ERROR_HANDLING_RESTORE() php_set_error_handling(EH_NORMAL, NULL TSRMLS_CC);
#endif

/* {{{ proto bool MemcachedLite::__construct ([string $persistent_id = null[, callable $callable]])
    Create a new object
*/
PHP_METHOD(memcachedlite, __construct)
{
	php_memc_lite_object *intern;
	char *persistent_id = NULL;
	int persistent_id_len = 0;
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;
	zend_bool rc, is_new = 0;

	fci.size = 0;

	PHP_MEMC_LITE_ERROR_HANDLING_INIT()
	PHP_MEMC_LITE_ERROR_HANDLING_THROW()

	rc = zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!f!", &persistent_id, &persistent_id_len, &fci, &fci_cache);

	PHP_MEMC_LITE_ERROR_HANDLING_RESTORE()

	if (rc == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->internal = s_memc_lite_internal_get (persistent_id, &is_new TSRMLS_CC);

	if (is_new) {
		if (fci.size) {
			if (!s_invoke_on_new_object (getThis(), &fci, &fci_cache, persistent_id TSRMLS_CC)) {
				memcached_free (intern->internal->memc);
				pefree (intern->internal, intern->internal->is_persistent);
				intern->internal = NULL;
				return;
			}

		}
		if (intern->internal->is_persistent)
			s_memc_lite_internal_store (persistent_id, intern->internal TSRMLS_CC);
	}
}
/* }}} */
#undef PHP_MEMC_LITE_ERROR_HANDLING_INIT
#undef PHP_MEMC_LITE_ERROR_HANDLING_THROW
#undef PHP_MEMC_LITE_ERROR_HANDLING_RESTORE

/* {{{ proto bool MemcachedLite::add_server(string $host[, int $port = 11211[, int $weight = 1]])
    Add a new server connection
*/
PHP_METHOD(memcachedlite, add_server)
{
	php_memc_lite_object *intern;
	char *host;
	int host_len;
	long port = 11211;
	memcached_return rc;
	long weight = 1;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l!", &host, &host_len, &port, &weight) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	rc = memcached_server_add_with_weight (intern->internal->memc, host, port, (uint32_t) weight);

	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::add_server", rc TSRMLS_CC)) {
		return;
	}
	RETURN_TRUE;
}
/* }}} */

static
memcached_return s_server_list_to_zval (const memcached_st *ptr, memc_lite_instance_st instance, void *context)
{
	zval *return_value, *server;

	return_value = (zval *) context;

	MAKE_STD_ZVAL (server);
	array_init (server);
	add_assoc_string (server, "host", (char *) memcached_server_name (instance), 1);
	add_assoc_long (server, "port", memcached_server_port (instance));

	add_next_index_zval (return_value, server);
	return MEMCACHED_SUCCESS;
}

/* {{{ proto array MemcachedLite::get_servers()
    Get a list of added servers
*/
PHP_METHOD(memcachedlite, get_servers)
{
	memcached_return rc;
	php_memc_lite_object *intern;
	memcached_server_function cb [1];

	if (zend_parse_parameters_none () == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	cb [0] =& s_server_list_to_zval;
	array_init (return_value);

	rc = memcached_server_cursor (intern->internal->memc, cb, (void *) return_value, 1);

	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::get_servers", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

/* {{{ proto array MemcachedLite::get_server ($key)
    Get the server entry where the key hashes to
*/
PHP_METHOD(memcachedlite, get_server)
{
	memcached_return rc = MEMCACHED_SUCCESS;
	php_memc_lite_object *intern;
	memc_lite_instance_st server;
	char *key;
	int key_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	server = memcached_server_by_key (intern->internal->memc, key, key_len, &rc);

	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::get_server", rc TSRMLS_CC)) {
		return;
	}

	array_init (return_value);
	add_assoc_string (return_value, "host", (char *) memcached_server_name (server), 1);
	add_assoc_long (return_value, "port", memcached_server_port (server));
	return;
}
/* }}} */

static
char *s_compress_value (const char *value, size_t *value_len)
{
	uint32_t uncompressed_size;
	zend_bool status;

	char *buffer = emalloc (sizeof (uint32_t) + (*value_len * 1.1));

	uncompressed_size = (uint32_t) *value_len;
	uncompressed_size = htonl (uncompressed_size);

	memcpy (buffer, &uncompressed_size, sizeof (uint32_t));

	buffer += sizeof (uint32_t);
	status  = ((*value_len = FASTLZ_FUNCTION_NAME(FASTLZ_FUNCTION_PREFIX, fastlz_compress) (value, (*value_len), buffer)) > 0);
	buffer -= sizeof (uint32_t);

	if (!status) {
		efree (buffer);
		return NULL;
	}
	*value_len += sizeof (uint32_t);
	return buffer;
}

static
char *s_marshall_value (zval *orig_value, size_t *length, uint32_t *flags, zend_bool *allocated, char *small_buffer, zend_bool compress TSRMLS_DC)
{
	char *value_ptr = NULL;

	*length    = -1;
	*flags     = 0;
	*allocated = 0;

	switch (Z_TYPE_P (orig_value)) {
		case IS_STRING:
			*flags   |= MEMC_LITE_FLAG_IS_STRING;
			*length   = Z_STRLEN_P (orig_value);
			value_ptr = Z_STRVAL_P (orig_value);
		break;

		case IS_LONG:
			*flags   |= MEMC_LITE_FLAG_IS_LONG;
			*length   = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%ld", Z_LVAL_P (orig_value));
			value_ptr = small_buffer;
		break;

		case IS_DOUBLE:
			*flags |= MEMC_LITE_FLAG_IS_DOUBLE;
			*length = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%.20f", Z_DVAL_P (orig_value));
			value_ptr = small_buffer;
		break;

		case IS_BOOL:
			*flags |= MEMC_LITE_FLAG_IS_BOOL;
			*length = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%d", (Z_BVAL_P (orig_value) ? 1 : 0));
			value_ptr = small_buffer;
		break;

		case IS_NULL:
			*flags |= MEMC_LITE_FLAG_IS_NULL;
			*length = 1;
			small_buffer [0] = 0;
			value_ptr = small_buffer;
		break;

		case IS_OBJECT:
		case IS_ARRAY:
		{
			smart_str buffer = {0};
			php_serialize_data_t var_hash;

			*flags |= MEMC_LITE_FLAG_IS_SERIALIZED;

			PHP_VAR_SERIALIZE_INIT (var_hash);
			php_var_serialize (&buffer, &orig_value, &var_hash TSRMLS_CC);
			PHP_VAR_SERIALIZE_DESTROY (var_hash);

			if (!buffer.c)
				return NULL;

			*length = buffer.len;

			// If this is a small value, just return using existing buffer
			if (buffer.len < MEMC_LITE_VERY_SMALL) {
				memcpy (small_buffer, buffer.c, buffer.len);
				value_ptr = small_buffer;
			} else {
				char *retval = estrdup (buffer.c);

				*allocated = 1;
				value_ptr = retval;
			}
			smart_str_free (&buffer);
		}
		break;
	}

	/* Whether to compress the value */
	if (compress && *length > MEMC_LITE_VERY_SMALL) {
		char *compressed = s_compress_value (value_ptr, length);

		if (!compressed) {
			if (*allocated) {
				efree (value_ptr);
			}
			return NULL;
		}

		if (*allocated) {
			efree (value_ptr);
		}
		value_ptr    = compressed;
		*allocated   = 1;
		*flags      |= MEMC_LITE_FLAG_IS_COMPRESSED;
	}
	return value_ptr;
}

static
uint64_t s_zval_to_uint64 (zval *cas TSRMLS_DC)
{
	switch (Z_TYPE_P (cas)) {
		case IS_LONG:
			if (Z_LVAL_P (cas) < 0)
				return 0;

			return (uint64_t) Z_LVAL_P (cas);
		break;

		case IS_DOUBLE:
			if (Z_DVAL_P (cas) < 0.0)
				return 0;

			return (uint64_t) Z_DVAL_P (cas);
		break;

		case IS_STRING:
		{
			uint64_t val;
			char *end;

			errno = 0;
			val = (uint64_t) strtoull (Z_STRVAL_P (cas), &end, 0);

			if (*end || (errno == ERANGE && val == UINT64_MAX) || (errno != 0 && val == 0)) {
				zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall cas token", -1 TSRMLS_CC);
				return 0;
			}
			return val;
		}
		break;
	}
	return 0;
}

/* {{{ proto bool MemcachedLite::set(string $key, mixed $value[, int $ttl = 0[, string $cas = null]])
    Set a key to a value
*/
PHP_METHOD(memcachedlite, set)
{
	char buffer [MEMC_LITE_VERY_SMALL];
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	zval *value;
	long ttl = 0;
	memcached_return rc = MEMCACHED_FAILURE;
	char *store_value;
	size_t store_value_len;
	uint32_t flags;
	zend_bool allocated;
	zval *cas = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|lz", &key, &key_len, &value, &ttl, &cas) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	store_value = s_marshall_value (value, &store_value_len, &flags, &allocated, buffer, intern->internal->compression TSRMLS_CC);

	if (store_value) {
		if (cas && Z_TYPE_P (cas) != IS_NULL) {
			memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);

			rc = memcached_cas (intern->internal->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags, s_zval_to_uint64 (cas TSRMLS_CC));

			memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 0);
		} else {
			rc = memcached_set (intern->internal->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags);
		}

		if (allocated) {
			efree (store_value);
		}
	} else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to marshall the value", -1 TSRMLS_CC);
		return;
	}

	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set", rc TSRMLS_CC)) {
		return;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool MemcachedLite::add(string $key, mixed $value[, int $ttl = 0])
    Add a key if it doesn't exist
*/
PHP_METHOD(memcachedlite, add)
{
	char buffer [MEMC_LITE_VERY_SMALL];
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	zval *value;
	long ttl = 0;
	memcached_return rc = MEMCACHED_FAILURE;
	char *store_value;
	size_t store_value_len;
	uint32_t flags;
	zend_bool allocated;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|l", &key, &key_len, &value, &ttl) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	store_value = s_marshall_value (value, &store_value_len, &flags, &allocated, buffer, intern->internal->compression TSRMLS_CC);

	if (store_value) {
		rc = memcached_add (intern->internal->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags);

		if (allocated) {
			efree (store_value);
		}
	} else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to marshall the value", -1 TSRMLS_CC);
		return;
	}

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key exists */
	else
	if (rc == MEMCACHED_NOTSTORED || rc == MEMCACHED_DATA_EXISTS) {
		RETURN_FALSE;
	}

	/* Seems like we have error */
	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::add", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

static
char *s_uncompress_value (const char *value, size_t *value_len TSRMLS_DC)
{
	zend_bool status;
	char *buffer;
	uint32_t original_size;
	memcpy (&original_size, value, sizeof (uint32_t));

	original_size = ntohl (original_size);

	/* Allocate decompression buffer */
	buffer = emalloc (original_size);

	value      += sizeof (uint32_t);
	*value_len -= sizeof (uint32_t);

	status = ((*value_len = FASTLZ_FUNCTION_NAME(FASTLZ_FUNCTION_PREFIX, fastlz_decompress) (value, *value_len, buffer, original_size)) > 0);

	if (!status) {
		efree (buffer);
		return NULL;
	}
	return buffer;
}

static
void s_unmarshall_value (zval *return_value, const char *value, size_t value_len, uint32_t flags TSRMLS_DC)
{
	char *decompressed = NULL;

	if (flags & MEMC_LITE_FLAG_IS_COMPRESSED) {
		decompressed = s_uncompress_value (value, &value_len TSRMLS_CC);

		if (!decompressed) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall compressed value", -1 TSRMLS_CC);
			return;
		}
		value = decompressed;
	}

	if (flags & MEMC_LITE_FLAG_IS_STRING) {
		ZVAL_STRINGL (return_value, value, value_len, 1);
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_LONG) {
		char *end = NULL;
		long l_val;

		if (value_len >= MEMC_LITE_VERY_SMALL) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall long value", -1 TSRMLS_CC);
		} else {
			char buffer [MEMC_LITE_VERY_SMALL];

			memcpy (buffer, value, value_len);
			buffer [value_len] = '\0';

			errno = 0;
			l_val = strtol (buffer, &end, 10);

			/* Let's see if there were errors */
			if (*end || (errno != 0 && l_val == 0)) {
				zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall long value", -1 TSRMLS_CC);
				return;
			}

			// No errors, let's see if it fits our range
			if ((errno == ERANGE && (l_val == LONG_MAX || l_val == LONG_MIN))) {
				// This is out of range for us, return as string
				ZVAL_STRING (return_value, buffer, value_len);
			} else {
				ZVAL_LONG (return_value, l_val);
			}
		}
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_DOUBLE) {
		if (value_len >= MEMC_LITE_VERY_SMALL) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall double value", -1 TSRMLS_CC);
		} else {
			char buffer [MEMC_LITE_VERY_SMALL];

			memcpy (buffer, value, value_len);
			buffer [value_len] = '\0';
			ZVAL_DOUBLE (return_value, zend_strtod (buffer, NULL));
		}
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_BOOL) {
		if (value_len != 1) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall boolean value", -1 TSRMLS_CC);
		} else {
			ZVAL_BOOL (return_value, value [0] == '1');
		}
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_NULL) {
		if (value_len != 1) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall null value", -1 TSRMLS_CC);
		} else {
			ZVAL_NULL (return_value);
		}
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_SERIALIZED) {
		zend_bool rc;
		php_unserialize_data_t var_hash;

		PHP_VAR_UNSERIALIZE_INIT(var_hash);
		rc = php_var_unserialize (&return_value, (const unsigned char **) &value, (const unsigned char *) value + value_len, &var_hash TSRMLS_CC);
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);

		if (!rc) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall serialized value", -1 TSRMLS_CC);
		}
	}
	else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall unknown value", -1 TSRMLS_CC);
	}

	if (decompressed) {
		efree (decompressed);
	}
}

static
void s_uint64_to_zval (zval *target, uint64_t value TSRMLS_DC)
{
	if (value >= LONG_MAX) {
		char *buffer;
		spprintf (&buffer, 0, "%" PRIu64, value);
		ZVAL_STRING (target, buffer, 0);
	}
	else {
		ZVAL_LONG (target, (long) value);
	}
}

/* {{{ proto mixed MemcachedLite::get(string $key[, bool $exists = null[, mixed $cas = null]])
    Set a key to a value
*/
PHP_METHOD(memcachedlite, get)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;

	memcached_return rc = MEMCACHED_SUCCESS;
	zval *exists = NULL;
	zval *cas = NULL;
	memcached_result_st result;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|zz", &key, &key_len, &exists, &cas) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	{
		const char const *keys [1] = { key };
		size_t keys_len [1] = { key_len };

		if (cas)
			memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);

		rc = memcached_mget (intern->internal->memc, keys, keys_len, 1);

		if (cas)
			memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 0);

		if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
			return;
		}

		rc = MEMCACHED_SUCCESS;
		memcached_result_create (intern->internal->memc, &result);

		if (memcached_fetch_result (intern->internal->memc, &result, &rc) != NULL) {
			if (cas) {
				zval_dtor (cas);
				s_uint64_to_zval (cas, memcached_result_cas (&result) TSRMLS_CC);
			}
			s_unmarshall_value (return_value, memcached_result_value (&result),
											  memcached_result_length (&result),
											  memcached_result_flags (&result) TSRMLS_CC);

			if (exists) {
				ZVAL_BOOL (exists, 1);
			}
			memcached_result_free (&result);
			return;
		}
	}

	if (exists) {
		ZVAL_BOOL (exists, 0);
	}

	if (cas) {
		zval_dtor (cas);
		ZVAL_NULL (cas);
	}

	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_NULL();
	}

	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */


static
memcached_return s_my_memcached_exist (memcached_st *memc, const char *key, int key_len)
{
#ifdef HAVE_MEMCACHED_EXIST
	return memcached_exist (memc, key, key_len);
#else
	memcached_return rc = MEMCACHED_SUCCESS;
	uint32_t flags = 0;
	size_t value_length = 0;
	char *value = NULL;

	value = memcached_get (memc, key, key_len, &value_length, &flags, &rc);
	if (value) {
		free (value);
	}
	return rc;
#endif
}


/* {{{ proto bool MemcachedLite::exist(string $key)
    Checks if the key exists
*/
PHP_METHOD(memcachedlite, exist)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	memcached_return rc;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	rc = s_my_memcached_exist (intern->internal->memc, key, key_len);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::exist", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

static
memcached_return s_my_memcached_touch (memcached_st *memc, const char *key, int key_len, time_t ttl)
{
#ifdef HAVE_MEMCACHED_TOUCH
	return memcached_touch (memc, key, key_len, ttl);
#else
	memcached_result_st result;
	memcached_return rc;
	const char const *keys [1] = { key };
	size_t keys_len [1] = { key_len };

	memcached_behavior_set (memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);
	rc = memcached_mget (memc, keys, keys_len, 1);
	memcached_behavior_set (memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 0);

	if (rc != MEMCACHED_SUCCESS)
		return rc;

	rc = MEMCACHED_NOTFOUND;
	memcached_result_create (memc, &result);

	if (memcached_fetch_result (memc, &result, &rc) != NULL) {
		rc = memcached_cas (memc, key, key_len,
				memcached_result_value (&result),
				memcached_result_length (&result),
				ttl,
				memcached_result_flags (&result),
				memcached_result_cas (&result));

		memcached_result_free (&result);
		return rc;
	}
	if (rc == MEMCACHED_END)
		return MEMCACHED_NOTFOUND;

	return rc;
#endif
}


/* {{{ proto bool MemcachedLite::touch(string $key[, int $ttl = 0])
    Update expiration time of a key without fetching the value
*/
PHP_METHOD(memcachedlite, touch)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	memcached_return rc;
	long ttl = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &key, &key_len, &ttl) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	rc = s_my_memcached_touch (intern->internal->memc, key, key_len, (time_t) ttl);

	if (rc == MEMCACHED_SUCCESS || rc == MEMCACHED_END) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::touch", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

/* {{{ proto bool MemcachedLite::delete(string $key)
    Delete a key
*/
PHP_METHOD(memcachedlite, delete)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	memcached_return rc;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	rc = memcached_delete (intern->internal->memc, key, key_len, 0);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::delete", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

static
void s_memc_lite_interval_op (INTERNAL_FUNCTION_PARAMETERS, php_memc_lite_op_t operation)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	memcached_return rc;
	long offset = 1, ttl = 0;
	long initial = 0;
	uint64_t new_value = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lll", &key, &key_len, &offset, &ttl, &initial) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	if (initial && !memcached_behavior_get (intern->internal->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL)) {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Initial value is only supported with binary protocol", -1 TSRMLS_CC);
		return;
	}

	if (initial > 0) {
		if (operation == PHP_MEMC_LITE_OP_INCR)
			rc = memcached_increment_with_initial (intern->internal->memc, key, key_len, offset, initial, (time_t) ttl, &new_value);
		else
			rc = memcached_decrement_with_initial (intern->internal->memc, key, key_len, offset, initial, (time_t) ttl, &new_value);
	}
	else {
		if (offset > UINT32_MAX) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "The offset is larger than maximum allowed value", -1 TSRMLS_CC);
			return;
		}
		if (operation == PHP_MEMC_LITE_OP_INCR)
			rc = memcached_increment (intern->internal->memc, key, key_len, offset, &new_value);
		else
			rc = memcached_decrement (intern->internal->memc, key, key_len, offset, &new_value);
	}

	if (rc == MEMCACHED_SUCCESS) {
		s_uint64_to_zval (return_value, new_value TSRMLS_CC);
		return;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->internal->memc, (operation == PHP_MEMC_LITE_OP_INCR) ? "MemcachedLite::increment" : "MemcachedLite::decrement", rc TSRMLS_CC)) {
		return;
	}
}

/* {{{ proto bool MemcachedLite::increment(string $key[, int $offset = 1])
    Increment a key
*/
PHP_METHOD(memcachedlite, increment)
{
	s_memc_lite_interval_op (INTERNAL_FUNCTION_PARAM_PASSTHRU, PHP_MEMC_LITE_OP_INCR);
}
/* }}} */

/* {{{ proto bool MemcachedLite::decrement(string $key[, int $offset = 1])
    Increment a key
*/
PHP_METHOD(memcachedlite, decrement)
{
	s_memc_lite_interval_op (INTERNAL_FUNCTION_PARAM_PASSTHRU, PHP_MEMC_LITE_OP_DECR);
}
/* }}} */

/* {{{ proto bool MemcachedLite::set_binary_protocol(bool $value)
    Turn on binary protocol
*/
PHP_METHOD(memcachedlite, set_binary_protocol)
{
	php_memc_lite_object *intern;
	memcached_return rc;
	zend_bool binary_proto;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &binary_proto) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	rc = memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, binary_proto);
	if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_binary_protocol", rc TSRMLS_CC)) {
		return;
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool MemcachedLite::get_binary_protocol()
    Check whether binary protocol is currently used
*/
PHP_METHOD(memcachedlite, get_binary_protocol)
{
	php_memc_lite_object *intern;
	uint64_t binary_proto;

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	binary_proto = memcached_behavior_get (intern->internal->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL);
	RETURN_BOOL (binary_proto == 1 ? 1 : 0);
}
/* }}} */

/* {{{ proto bool MemcachedLite::set_distribution(int $value)
    Set key distribution method
*/
PHP_METHOD(memcachedlite, set_distribution)
{
	php_memc_lite_object *intern;
	memcached_return rc;
	long distrib;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &distrib) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	if (distrib > PHP_MEMC_LITE_DISTRIBUTION_MIN && distrib < PHP_MEMC_LITE_DISTRIBUTION_MAX) {
		php_memc_lite_distribution_t distribution = (php_memc_lite_distribution_t) distrib;

		switch (distribution) {
			case PHP_MEMC_LITE_DISTRIBUTION_MODULO:
				rc = memcached_behavior_set (intern->internal->memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_MODULA);
				if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}

				rc = memcached_behavior_set (intern->internal->memc, MEMCACHED_BEHAVIOR_HASH, MEMCACHED_HASH_DEFAULT);
				if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}
				intern->internal->distribution = distribution;
			break;

			case PHP_MEMC_LITE_DISTRIBUTION_KETAMA:
				rc = memcached_behavior_set (intern->internal->memc, MEMCACHED_BEHAVIOR_KETAMA_WEIGHTED, 1);
				if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}
				intern->internal->distribution = distribution;
			break;

			default:
				RETURN_FALSE;
			break;
		}
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool MemcachedLite::get_distribution()
    Get key distribution method
*/
PHP_METHOD(memcachedlite, get_distribution)
{
	php_memc_lite_object *intern;

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	RETURN_LONG(intern->internal->distribution);
}
/* }}} */

/* {{{ proto bool MemcachedLite::set_compression(bool $value)
    Set value compression value
*/
PHP_METHOD(memcachedlite, set_compression)
{
	php_memc_lite_object *intern;
	zend_bool compression;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &compression) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->internal->compression = compression;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool MemcachedLite::get_compression()
    Get value compression value
*/
PHP_METHOD(memcachedlite, get_compression)
{
	php_memc_lite_object *intern;

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	RETURN_BOOL (intern->internal->compression);
}
/* }}} */

#ifdef HAVE_MEMC_LITE_SASL
/* {{{ proto bool MemcachedLite::set_sasl_credentials(string $username, string $password)
    Set SASL credentials
*/
PHP_METHOD(memcachedlite, set_sasl_credentials)
{
	memcached_return rc;
	php_memc_lite_object *intern;
	char *username, *password;
	int username_len, password_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss", &username, &username_len, &password, &password_len) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	// Have previous credentials
	if (intern->internal->sasl_credentials.username && intern->internal->sasl_credentials.password) {
		pefree (intern->internal->sasl_credentials.username, intern->internal->is_persistent);
		pefree (intern->internal->sasl_credentials.password, intern->internal->is_persistent);

		intern->internal->sasl_credentials.username = NULL;
		intern->internal->sasl_credentials.password = NULL;

		memcached_destroy_sasl_auth_data (intern->internal->memc);
	}

	if (username_len > 0 && password_len > 0) {
		if (!memcached_behavior_get (intern->internal->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL)) {
			rc = memcached_behavior_set(intern->internal->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);

			if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_sasl_credentials", rc TSRMLS_CC)) {
				return;
			}
		}
		intern->internal->sasl_credentials.username = pestrdup (username, intern->internal->is_persistent);
		intern->internal->sasl_credentials.password = pestrdup (password, intern->internal->is_persistent);

		rc = memcached_set_sasl_auth_data (intern->internal->memc, username, password);

		if (s_handle_libmemcached_return (intern->internal->memc, "MemcachedLite::set_sasl_credentials", rc TSRMLS_CC)) {
			return;
		}
	}
	RETURN_TRUE;
}

/* }}} */

/* {{{ proto array MemcachedLite::get_sasl_credentials()
    Get SASL credentials
*/
PHP_METHOD(memcachedlite, get_sasl_credentials)
{
	php_memc_lite_object *intern;

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	if (intern->internal->sasl_credentials.username && intern->internal->sasl_credentials.password) {
		array_init (return_value);
		add_assoc_string (return_value, "username", intern->internal->sasl_credentials.username, 1);
		add_assoc_string (return_value, "password", intern->internal->sasl_credentials.password, 1);
		return;
	}
	RETURN_FALSE;
}
/* }}} */
#endif

ZEND_BEGIN_ARG_INFO_EX(memc_lite_construct_args, 0, 0, 0)
	ZEND_ARG_INFO(0, persistent_id)
	ZEND_ARG_INFO(0, on_new)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_add_server_args, 0, 0, 1)
	ZEND_ARG_INFO(0, host)
	ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_servers_args, 0, 0, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_server_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_args, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_add_args, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(1, exists)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_exist_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_touch_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_delete_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_increment_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_decrement_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_binary_protocol_args, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_binary_protocol_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_distribution_args, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_distribution_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_compression_args, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_compression_args, 0, 0, 0)
ZEND_END_ARG_INFO()

#ifdef HAVE_MEMC_LITE_SASL
ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_sasl_credentials, 0, 0, 2)
	ZEND_ARG_INFO(0, username)
	ZEND_ARG_INFO(0, password)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_sasl_credentials, 0, 0, 0)
ZEND_END_ARG_INFO()
#endif

static
zend_function_entry php_memc_lite_class_methods[] =
{
	PHP_ME(memcachedlite, __construct, memc_lite_construct_args,   ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, add_server,  memc_lite_add_server_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_servers, memc_lite_get_servers_args, ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_server,  memc_lite_get_server_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, set,         memc_lite_set_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, add,         memc_lite_add_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get,         memc_lite_get_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, exist,       memc_lite_exist_args,       ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, touch,       memc_lite_touch_args,       ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, delete,      memc_lite_delete_args,      ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, increment,   memc_lite_increment_args,   ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, decrement,   memc_lite_decrement_args,   ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_binary_protocol,  memc_lite_set_binary_protocol_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_binary_protocol,  memc_lite_get_binary_protocol_args,  ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_distribution,     memc_lite_set_distribution_args,     ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_distribution,     memc_lite_get_distribution_args,     ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_compression,      memc_lite_set_compression_args,      ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_compression,      memc_lite_get_compression_args,      ZEND_ACC_PUBLIC)
#ifdef HAVE_MEMC_LITE_SASL
	PHP_ME(memcachedlite, set_sasl_credentials, memc_lite_set_sasl_credentials,      ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_sasl_credentials, memc_lite_get_sasl_credentials,      ZEND_ACC_PUBLIC)
#endif
	{ NULL, NULL, NULL }
};

static
void php_memc_lite_object_free_storage(void *object TSRMLS_DC)
{
	php_memc_lite_object *intern = (php_memc_lite_object *)object;

	if (!intern) {
		return;
	}

	if (intern->internal && !intern->internal->is_persistent) {
#ifdef HAVE_MEMC_LITE_SASL
	if (intern->internal->sasl_credentials.username && intern->internal->sasl_credentials.password) {
		pefree (intern->internal->sasl_credentials.username, intern->internal->is_persistent);
		pefree (intern->internal->sasl_credentials.password, intern->internal->is_persistent);

		intern->internal->sasl_credentials.username = NULL;
		intern->internal->sasl_credentials.password = NULL;

		memcached_destroy_sasl_auth_data (intern->internal->memc);
	}
#endif
		memcached_free (intern->internal->memc);
		pefree (intern->internal, 0);
	}

	zend_object_std_dtor(&intern->zo TSRMLS_CC);
	efree(intern);
}

#if PHP_VERSION_ID < 50399 && !defined(object_properties_init)
# define object_properties_init(zo, class_type) { \
			zval *tmp; \
			zend_hash_copy((*zo).properties, \
							&class_type->default_properties, \
							(copy_ctor_func_t) zval_add_ref, \
							(void *) &tmp, \
							sizeof(zval *)); \
		 }
#endif

static
zend_object_value php_memc_lite_object_new(zend_class_entry *class_type TSRMLS_DC)
{
	zend_object_value retval;
	php_memc_lite_object *intern = emalloc (sizeof (php_memc_lite_object));

	memset(&intern->zo, 0, sizeof(zend_object));

	intern->internal = NULL;

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	object_properties_init(&intern->zo, class_type);

	retval.handle   = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_memc_lite_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = &memc_lite_object_handlers;
	return retval;
}

ZEND_RSRC_DTOR_FUNC(s_memc_lite_internal_dtor)
{
	if (rsrc->ptr) {
		php_memc_lite_internal_t *internal = (php_memc_lite_internal_t *) rsrc->ptr;

#ifdef HAVE_MEMC_LITE_SASL
		if (internal->sasl_credentials.username && internal->sasl_credentials.password) {
			pefree (internal->sasl_credentials.username, internal->is_persistent);
			pefree (internal->sasl_credentials.password, internal->is_persistent);

			internal->sasl_credentials.username = NULL;
			internal->sasl_credentials.password = NULL;

			memcached_destroy_sasl_auth_data (internal->memc);
		}
#endif
		if (internal->memc)
			memcached_free (internal->memc);

		internal->memc = NULL;
		pefree (internal, internal->is_persistent);
		rsrc->ptr = NULL;
	}
}

PHP_MINIT_FUNCTION(memc_lite)
{
	zend_class_entry ce;
#ifdef HAVE_MEMC_LITE_SASL
	if (sasl_client_init (NULL) != SASL_OK) {
		zend_error (E_ERROR, "Failed to initialize SASL client");
	}
#endif
	memcpy (&memc_lite_object_handlers, zend_get_std_object_handlers (), sizeof (zend_object_handlers));

	/* Register destructors for persistent connections */
	le_memc_lite_internal_st = zend_register_list_destructors_ex(NULL, &s_memc_lite_internal_dtor, "Memcached Lite persistent connection", module_number);


	INIT_CLASS_ENTRY(ce, "MemcachedLiteException", NULL);
	php_memc_lite_exception_sc_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
	php_memc_lite_exception_sc_entry->ce_flags |= ZEND_ACC_FINAL;

	INIT_CLASS_ENTRY(ce, "MemcachedLite", php_memc_lite_class_methods);
	ce.create_object                      = php_memc_lite_object_new;
	memc_lite_object_handlers.clone_obj   = NULL;
	php_memc_lite_sc_entry                = zend_register_internal_class(&ce TSRMLS_CC);

#define PHP_MEMC_LITE_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_memc_lite_sc_entry, const_name, sizeof (const_name) - 1, (long) value TSRMLS_CC);

	PHP_MEMC_LITE_REGISTER_CONST_LONG ("DISTRIBUTION_MODULO",  PHP_MEMC_LITE_DISTRIBUTION_MODULO);
	PHP_MEMC_LITE_REGISTER_CONST_LONG ("DISTRIBUTION_KETAMA",  PHP_MEMC_LITE_DISTRIBUTION_KETAMA);

#ifdef HAVE_MEMC_LITE_SASL
	PHP_MEMC_LITE_REGISTER_CONST_LONG ("HAVE_SASL", 1);
#else
	PHP_MEMC_LITE_REGISTER_CONST_LONG ("HAVE_SASL", 0);
#endif

#undef PHP_MEMC_LITE_REGISTER_CONST_LONG
	return SUCCESS;
}

PHP_MINFO_FUNCTION(memc_lite)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "memc-lite", "enabled");
	php_info_print_table_row(2, "memc-lite module version", PHP_MEMC_LITE_VERSION);
	php_info_print_table_end();
}

static
zend_function_entry php_memc_lite_functions [] =
{
	{ NULL, NULL, NULL }
};

zend_module_entry memc_lite_module_entry =
{
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	PHP_MEMC_LITE_EXTNAME,
	php_memc_lite_functions,                  /* Functions */
	PHP_MINIT(memc_lite),                     /* MINIT */
	NULL,                                     /* MSHUTDOWN */
	NULL,                                     /* RINIT */
	NULL,                                     /* RSHUTDOWN */
	PHP_MINFO(memc_lite),                     /* MINFO */
	PHP_MEMC_LITE_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MEMC_LITE
ZEND_GET_MODULE(memc_lite)
#endif
