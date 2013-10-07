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
#include "Zend/zend_exceptions.h"

#include <libmemcached/memcached.h>

typedef struct _php_memc_lite_object  {
	zend_object zo;
	memcached_st *memc;
} php_memc_lite_object;

static
	zend_class_entry *php_memc_lite_sc_entry, *php_memc_lite_exception_sc_entry;

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

/* {{{ proto bool MemcachedLite::add_server(string $host[, int $port = 11211])
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


ZEND_BEGIN_ARG_INFO_EX(memc_lite_add_server_args, 0, 0, 1)
	ZEND_ARG_INFO(0, host)
	ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

static
zend_function_entry php_memc_lite_class_methods[] =
{
	PHP_ME(memcachedlite, add_server, memc_lite_add_server_args, ZEND_ACC_PUBLIC)
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
