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

#include "fastlz/fastlz.h"

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be16toh(x) betoh16(x)
#  define be32toh(x) betoh32(x)
#  define be64toh(x) betoh64(x)
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define htobe64(x) OSSwapHostToBigInt64(x)
#  define be64toh(x) OSSwapBigToHostInt64(x)
#endif

static
	zend_class_entry *php_memc_lite_sc_entry,
					 *php_memc_lite_exception_sc_entry;

static
	zend_object_handlers memc_lite_object_handlers;

typedef enum _php_memc_lite_distribution_t {
	PHP_MEMC_LITE_DISTRIBUTION_MIN,
	PHP_MEMC_LITE_DISTRIBUTION_MODULO,
	PHP_MEMC_LITE_DISTRIBUTION_KETAMA,
	PHP_MEMC_LITE_DISTRIBUTION_VBUCKET,
	PHP_MEMC_LITE_DISTRIBUTION_MAX
} php_memc_lite_distribution_t;

typedef struct _php_memc_lite_object  {
	zend_object zo;
	memcached_st *memc;
	php_memc_lite_distribution_t distribution;
	zend_bool compression;
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

	zend_throw_exception_ex (php_memc_lite_exception_sc_entry, rc TSRMLS_CC, "Call to %s failed: %s", name, memcached_strerror (memc, rc));
	return 1;
}

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
	rc = memcached_server_add_with_weight (intern->memc, host, port, (uint32_t) weight);

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::add_server", rc TSRMLS_CC)) {
		return;
	}
	RETURN_TRUE;
}
/* }}} */

static
memcached_return s_server_list_to_zval (const memcached_st *ptr, const memcached_instance_st *instance, void *context)
{
	zval *return_value, *server;

	return_value = (zval *) context;

	MAKE_STD_ZVAL (server);
	array_init (server);
	add_assoc_string (server, "host", memcached_server_name (instance), 1);
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

	rc = memcached_server_cursor (intern->memc, cb, (void *) return_value, 1);

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::add_server", rc TSRMLS_CC)) {
		return;
	}
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
	status  = ((*value_len = fastlz_compress (value, (*value_len), buffer)) > 0);
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
				smart_str_free (&buffer);
				value_ptr = small_buffer;
			} else {
				char *retval;
				retval  = estrdup (buffer.c);
				smart_str_free (&buffer);

				*allocated = 1;
				value_ptr = retval;
			}
		}
		break;
	}

	/* Whether to compress the value */
	if (compress && *length > MEMC_LITE_VERY_SMALL) {
		char *compressed = s_compress_value (value_ptr, length TSRMLS_CC);

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
void s_uint64_to_zval (zval *target, uint64_t value TSRMLS_DC)
{
	char *buffer;
	uint64_t big_endian = htobe64 (value);

	buffer = emalloc (sizeof (uint64_t));
	memcpy (buffer, &big_endian, sizeof (uint64_t));
	ZVAL_STRINGL (target, buffer, sizeof (uint64_t), 0);
}

static
uint64_t s_zval_to_uint64 (zval *source TSRMLS_DC)
{
	uint64_t big_endian;

	if (Z_TYPE_P (source) != IS_STRING || Z_STRLEN_P (source) != sizeof (uint64_t)) {
		return 0;
	}
	memcpy (&big_endian, Z_STRVAL_P (source), sizeof (uint64_t));
	return be64toh (big_endian);
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
	store_value = s_marshall_value (value, &store_value_len, &flags, &allocated, buffer, intern->compression TSRMLS_CC);

	if (store_value) {
		if (cas && Z_TYPE_P (cas) != IS_NULL) {
			memcached_behavior_set(intern->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);

			rc = memcached_cas (intern->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags, s_zval_to_uint64 (cas));

			memcached_behavior_set(intern->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 0);
		} else {
			rc = memcached_set (intern->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags);
		}

		if (allocated) {
			efree (store_value);
		}
	} else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to marshall the value", -1 TSRMLS_CC);
		return;
	}

	/* For some reason some values give MEMCACHED_END when binary protocol is used */
	if (memcached_behavior_get (intern->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL) && rc == MEMCACHED_END) {
		RETURN_TRUE;
	}

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set", rc TSRMLS_CC)) {
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
	store_value = s_marshall_value (value, &store_value_len, &flags, &allocated, buffer, intern->compression TSRMLS_CC);

	if (store_value) {
		rc = memcached_add (intern->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags);

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
	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::add", rc TSRMLS_CC)) {
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

	/* Let's make sure that the returned value length is not disproportional.
	   If original_size is half of the compressed size then something has gone wrong
	*/
	if (original_size < (*value_len * 2)) {
		return NULL;
	}

	/* Allocate decompression buffer */
	buffer = emalloc (original_size);

	value      += sizeof (uint32_t);
	*value_len -= sizeof (uint32_t);

	status = ((*value_len = fastlz_decompress (value, *value_len, buffer, original_size)) > 0);

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
		char *end;
		long l_val;

		l_val = strtol (value, &end, 10);

		if ((errno == ERANGE && (l_val == LONG_MAX || l_val == LONG_MIN)) ||
		    (errno != 0 && l_val == 0)) {
		   zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall long value", -1 TSRMLS_CC);
		} else {
			ZVAL_LONG (return_value, l_val);
		}
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_DOUBLE) {
		double d_val = zend_strtod (value, NULL);
		ZVAL_DOUBLE (return_value, d_val);
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

/* {{{ proto mixed MemcachedLite::get(string $key[, bool $exists = null])
    Set a key to a value
*/
PHP_METHOD(memcachedlite, get)
{
	php_memc_lite_object *intern;
	char *key;
	int key_len;
	char *value;
	size_t value_len;
	uint32_t flags;
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
			memcached_behavior_set(intern->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 1);

		rc = memcached_mget (intern->memc, keys, keys_len, 1);

		if (cas)
			memcached_behavior_set(intern->memc, MEMCACHED_BEHAVIOR_SUPPORT_CAS, 0);

		if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
			return;
		}

		rc = MEMCACHED_SUCCESS;
		memcached_result_create (intern->memc, &result);

		if (memcached_fetch_result (intern->memc, &result, &rc) != NULL) {
			if (cas)
				s_uint64_to_zval (cas, memcached_result_cas (&result));

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
		ZVAL_NULL (cas);
	}

	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_NULL();
	}

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

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
	rc = memcached_exist (intern->memc, key, key_len);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::exist", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

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
	rc = memcached_touch (intern->memc, key, key_len, (time_t) ttl);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::touch", rc TSRMLS_CC)) {
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
	rc = memcached_delete (intern->memc, key, key_len, 0);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_TRUE;
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::delete", rc TSRMLS_CC)) {
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
	uint64_t initial = 0, new_value = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|ll", &key, &key_len, &offset, &ttl) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	if (operation == PHP_MEMC_LITE_OP_INCR)
		rc = memcached_increment_with_initial (intern->memc, key, key_len, offset, initial, (time_t) ttl, &new_value);
	else
		rc = memcached_decrement_with_initial (intern->memc, key, key_len, offset, initial, (time_t) ttl, &new_value);

	if (rc == MEMCACHED_SUCCESS) {
		RETURN_LONG (new_value);
	}
	/* No exception on this, just means that key doesnt exist */
	else
	if (rc == MEMCACHED_NOTFOUND) {
		RETURN_FALSE;
	}
	if (s_handle_libmemcached_return (intern->memc, (operation == PHP_MEMC_LITE_OP_INCR) ? "MemcachedLite::increment" : "MemcachedLite::decrement", rc TSRMLS_CC)) {
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

	rc = memcached_behavior_set(intern->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, binary_proto);
	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set_binary_protocol", rc TSRMLS_CC)) {
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

	binary_proto = memcached_behavior_get (intern->memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL);
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
				rc = memcached_behavior_set (intern->memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_MODULA);
				if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}

				rc = memcached_behavior_set (intern->memc, MEMCACHED_BEHAVIOR_HASH, MEMCACHED_HASH_DEFAULT);
				if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}
			break;

			case PHP_MEMC_LITE_DISTRIBUTION_KETAMA:
				rc = memcached_behavior_set (intern->memc, MEMCACHED_BEHAVIOR_KETAMA_WEIGHTED, 1);
				if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}
			break;

			case PHP_MEMC_LITE_DISTRIBUTION_VBUCKET:
				rc = memcached_behavior_set_distribution (intern->memc, MEMCACHED_DISTRIBUTION_VIRTUAL_BUCKET);
				if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set_distribution", rc TSRMLS_CC)) {
					return;
				}
			break;

			default:
			break;
		}
		intern->distribution = distribution;
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
	RETURN_LONG(intern->distribution);
}
/* }}} */

/* {{{ proto bool MemcachedLite::set_compression(bool $value)
    Set value compression value
*/
PHP_METHOD(memcachedlite, set_compression)
{
	php_memc_lite_object *intern;
	memcached_return rc;
	zend_bool compression;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &compression) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->compression = compression;

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
	RETURN_BOOL (intern->compression);
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(memc_lite_add_server_args, 0, 0, 1)
	ZEND_ARG_INFO(0, host)
	ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_servers_args, 0, 0, 1)
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

static
zend_function_entry php_memc_lite_class_methods[] =
{
	PHP_ME(memcachedlite, add_server,  memc_lite_add_server_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_servers, memc_lite_get_servers_args, ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, set,         memc_lite_set_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, add,         memc_lite_add_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get,         memc_lite_get_args,         ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, exist,       memc_lite_exist_args,       ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, touch,       memc_lite_touch_args,       ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, delete,      memc_lite_delete_args,      ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, increment,   memc_lite_increment_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, decrement,   memc_lite_decrement_args,  ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_binary_protocol,  memc_lite_set_binary_protocol_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_binary_protocol,  memc_lite_get_binary_protocol_args,  ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_distribution,  memc_lite_set_distribution_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_distribution,  memc_lite_get_distribution_args,  ZEND_ACC_PUBLIC)

	PHP_ME(memcachedlite, set_compression,  memc_lite_set_compression_args,  ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get_compression,  memc_lite_get_compression_args,  ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

static
void php_memc_lite_object_free_storage(void *object TSRMLS_DC)
{
	php_memc_lite_object *intern = (php_memc_lite_object *)object;

	if (!intern) {
		return;
	}

	if (intern->memc)
		memcached_free (intern->memc);

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

	// Create a new memcached_st struct
	intern->memc = memcached_create (NULL);

	if (!intern->memc) {
		zend_error (E_ERROR, "Failed to allocate memory for struct memcached_st");
	}

	intern->compression  = 0;
	intern->distribution = PHP_MEMC_LITE_DISTRIBUTION_MODULO;

	memcached_behavior_set (intern->memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_MODULA);
	memcached_behavior_set (intern->memc, MEMCACHED_BEHAVIOR_HASH, MEMCACHED_HASH_DEFAULT);

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	object_properties_init(&intern->zo, class_type);

	retval.handle   = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_memc_lite_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = &memc_lite_object_handlers;
	return retval;
}

PHP_MINIT_FUNCTION(memc_lite)
{
	zend_class_entry ce;
	memcpy (&memc_lite_object_handlers, zend_get_std_object_handlers (), sizeof (zend_object_handlers));

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
	PHP_MEMC_LITE_REGISTER_CONST_LONG ("DISTRIBUTION_VBUCKET", PHP_MEMC_LITE_DISTRIBUTION_VBUCKET);

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
