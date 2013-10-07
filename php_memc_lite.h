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

#ifndef PHP_MEMC_LITE_H
#  define PHP_MEMC_LITE_H

/* Define Extension Properties */
#define PHP_MEMC_LITE_EXTNAME    "memc_lite"
#define PHP_MEMC_LITE_VERSION    "@PACKAGE_VERSION@"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ZTS
# include "TSRM.h"
#endif

#include "php.h"

extern zend_module_entry memc_lite_module_entry;
#define phpext_memc_lite_ptr &memc_lite_module_entry

#endif /* PHP_MEMC_LITE_H */