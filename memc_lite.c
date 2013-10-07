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

typedef struct _php_memc_lite_object  {
	zend_object zo;
	memcached_st *memc;
} php_memc_lite_object;

static
	zend_class_entry *php_memc_lite_sc_entry,
					 *php_memc_lite_exception_sc_entry;

static
	zend_object_handlers memc_lite_object_handlers;

typedef enum _php_memc_lite_distribution {
	PHP_MEMC_LITE_DISTRIBUTION_MODULO  = 1,
	PHP_MEMC_LITE_DISTRIBUTION_KETAMA  = 2,
	PHP_MEMC_LITE_DISTRIBUTION_VBUCKET = 3,
} php_memc_lite_distribution;

#if PHP_VERSION_ID < 50399
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
zend_bool s_handle_libmemcached_return (memcached_st *memc, const char *name, memcached_return rc TSRMLS_DC)
{
	// No error
	if (rc == MEMCACHED_SUCCESS)
		return 0;

	zend_throw_exception_ex (php_memc_lite_exception_sc_entry, rc TSRMLS_CC, "Call to %s failed: %s", name, memcached_strerror (memc, rc));
	return 1;
}

/* {{{ proto MemcachedLite MemcachedLite::add_server(string $host[, int $port = 11211])
    Add a new server connection
*/
PHP_METHOD(memcachedlite, add_server)
{
	php_memc_lite_object *intern;
	char *host;
	int host_len;
	long port = 11211;
	memcached_return rc;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l!", &host, &host_len, &port) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	rc = memcached_server_add (intern->memc, host, port);

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::add_server", rc TSRMLS_CC)) {
		return;
	}

	RETURN_ZVAL (getThis (), 1, 0);
}
/* }}} */

#define MEMC_LITE_FLAG_IS_STRING     (1 << 1)
#define MEMC_LITE_FLAG_IS_LONG       (1 << 2)
#define MEMC_LITE_FLAG_IS_DOUBLE     (1 << 3)
#define MEMC_LITE_FLAG_IS_BOOL       (1 << 4)
#define MEMC_LITE_FLAG_IS_NULL       (1 << 5)
#define MEMC_LITE_FLAG_IS_SERIALIZED (1 << 6)
#define MEMC_LITE_FLAG_IS_COMPRESSED (1 << 7)

#define MEMC_LITE_VERY_SMALL 48

static
char *s_marshall_value (zval *orig_value, size_t *length, uint32_t *flags, zend_bool *allocated, char *small_buffer TSRMLS_DC)
{
	*length    = -1;
	*flags     = 0;
	*allocated = 0;

	switch (Z_TYPE_P (orig_value)) {
		case IS_STRING:
			*flags |= MEMC_LITE_FLAG_IS_STRING;
			*length = Z_STRLEN_P (orig_value);
			return    Z_STRVAL_P (orig_value);
		break;

		case IS_LONG:
			*flags |= MEMC_LITE_FLAG_IS_LONG;
			*length = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%ld", Z_LVAL_P (orig_value));
			return small_buffer;
		break;

		case IS_DOUBLE:
			*flags |= MEMC_LITE_FLAG_IS_DOUBLE;
			*length = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%.20f", Z_DVAL_P (orig_value));
			return small_buffer;
		break;

		case IS_BOOL:
			*flags |= MEMC_LITE_FLAG_IS_BOOL;
			*length = snprintf (small_buffer, MEMC_LITE_VERY_SMALL, "%d", (Z_BVAL_P (orig_value) ? 1 : 0));
			return small_buffer;
		break;

		case IS_NULL:
			*flags |= MEMC_LITE_FLAG_IS_NULL;
			*length = 1;
			small_buffer [0] = 0;
			return small_buffer;
		break;

		case IS_OBJECT:
		case IS_ARRAY:
		{
			char *retval;
			smart_str buffer = {0};
			php_serialize_data_t var_hash;

			*flags |= MEMC_LITE_FLAG_IS_SERIALIZED;

			PHP_VAR_SERIALIZE_INIT (var_hash);
			php_var_serialize (&buffer, &orig_value, &var_hash TSRMLS_CC);
			PHP_VAR_SERIALIZE_DESTROY (var_hash);

			if (!buffer.c)
				return NULL;

			*length = buffer.len;
			retval  = estrdup (buffer.c); // TODO: kinda useless copy here
			smart_str_free (&buffer);

			*allocated = 1;
			return retval;
		}
		break;
	}
	return NULL;
}

/* {{{ proto MemcachedLite MemcachedLite::set(string $key, mixed $value[, int $ttl = 0])
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

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|l", &key, &key_len, &value, &ttl) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);
	store_value = s_marshall_value (value, &store_value_len, &flags, &allocated, buffer TSRMLS_CC);

	if (store_value) {
		rc = memcached_set (intern->memc, key, key_len, store_value, store_value_len, (time_t) ttl, flags);

		if (allocated) {
			efree (store_value);
		}
	} else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to marshall the value for saving", -1 TSRMLS_CC);
		return;
	}

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::set", rc TSRMLS_CC)) {
		return;
	}
	RETURN_ZVAL (getThis (), 1, 0);
}
/* }}} */

static
void s_unmarshall_value (zval *return_value, const char *value, size_t value_len, uint32_t flags TSRMLS_DC)
{
	if (flags & MEMC_LITE_FLAG_IS_STRING) {
		ZVAL_STRING (return_value, value, value_len);
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_LONG) {
		char *end;
		long l_val;

		l_val = strtol (value, &end, 10);

		if ((errno == ERANGE && (l_val == LONG_MAX || l_val == LONG_MIN)) ||
		    (errno != 0 && l_val == 0)) {
		   zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall long value", -1 TSRMLS_CC);
           return;
		}
		ZVAL_LONG (return_value, l_val);
		return;
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_DOUBLE) {
		double d_val = zend_strtod (value, NULL);
		ZVAL_DOUBLE (return_value, d_val);
		return;
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_BOOL) {
		if (value_len != 1) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall boolean value", -1 TSRMLS_CC);
			return;
		}
		ZVAL_BOOL (return_value, value [0] == '1');
		return;
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_NULL) {
		if (value_len != 1) {
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall null value", -1 TSRMLS_CC);
			return;
		}
		ZVAL_NULL (return_value);
		return;
	}
	else
	if (flags & MEMC_LITE_FLAG_IS_SERIALIZED) {
		php_unserialize_data_t var_hash;

		PHP_VAR_UNSERIALIZE_INIT(var_hash);
		if (!php_var_unserialize(&return_value, (const unsigned char **) &value, (const unsigned char *) value + value_len, &var_hash TSRMLS_CC)) {
			PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
			zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall serialized value", -1 TSRMLS_CC);
			return;
		}
		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
	}
	else {
		zend_throw_exception (php_memc_lite_exception_sc_entry, "Failed to unmarshall unknown value", -1 TSRMLS_CC);
		return;
	}
}

/* {{{ proto MemcachedLite MemcachedLite::get(string $key[, bool $exists = null])
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

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|z", &key, &key_len, &exists) == FAILURE) {
		return;
	}

	intern = (php_memc_lite_object *) zend_object_store_get_object(getThis() TSRMLS_CC);

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
		return;
	}

	value = memcached_get (intern->memc, key, key_len, &value_len, &flags, &rc);

	if (rc == MEMCACHED_SUCCESS) {
		s_unmarshall_value (return_value, value, value_len, flags TSRMLS_CC);
		if (exists) {
			ZVAL_BOOL (exists, 1);
		}
		free (value);
		return;
	}

	/* Not found is expected during get operation, hence no exception here */
	if (rc == MEMCACHED_NOTFOUND) {
		if (exists) {
			ZVAL_BOOL (exists, 0);
		}
		RETURN_NULL();
	}

	if (s_handle_libmemcached_return (intern->memc, "MemcachedLite::get", rc TSRMLS_CC)) {
		return;
	}
}
/* }}} */

ZEND_BEGIN_ARG_INFO_EX(memc_lite_add_server_args, 0, 0, 1)
	ZEND_ARG_INFO(0, host)
	ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_set_args, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, ttl)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(memc_lite_get_args, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(1, exists)
ZEND_END_ARG_INFO()

static
zend_function_entry php_memc_lite_class_methods[] =
{
	PHP_ME(memcachedlite, add_server, memc_lite_add_server_args, ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, set,        memc_lite_set_args,        ZEND_ACC_PUBLIC)
	PHP_ME(memcachedlite, get,        memc_lite_get_args,        ZEND_ACC_PUBLIC)
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
