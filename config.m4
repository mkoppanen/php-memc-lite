
PHP_ARG_WITH(memc-lite, whether to enable memc-lite support,
[  --with-memc-lite=[PATH]         Enable memc-lite support])


if test "$PHP_MEMC_LITE" != "no"; then

  AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
  if test "x$PKG_CONFIG" = "xno"; then
    AC_MSG_RESULT([pkg-config not found])
    AC_MSG_ERROR([Please reinstall the pkg-config distribution])
  fi 
 
  AC_MSG_CHECKING(libmemcached installation)
  if test "$PHP_MEMC_LITE" != "no" && test "$PHP_MEMC_LITE" != "yes"; then
    export PKG_CONFIG_PATH="$PHP_MEMC_LITE:$PHP_MEMC_LITE/lib/pkgconfig"
  else
    if test "x${PKG_CONFIG_PATH}" != "x"; then
      export PKG_CONFIG_PATH="${PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/opt/lib/pkgconfig:/opt/local/lib/pkgconfig"
    else
      export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/opt/lib/pkgconfig:/opt/local/lib/pkgconfig"
    fi
  fi

  if $PKG_CONFIG --exists libmemcached; then
    PHP_MEMC_LITE_VERSION=`$PKG_CONFIG libmemcached --modversion`
    PHP_MEMC_LITE_PREFIX=`$PKG_CONFIG libmemcached --variable=prefix`

    AC_MSG_RESULT([found version $PHP_MEMC_LITE_VERSION, under $PHP_MEMC_LITE_PREFIX])
    PHP_MEMC_LITE_LIBS=`$PKG_CONFIG libmemcached --libs`
    PHP_MEMC_LITE_CFLAGS=`$PKG_CONFIG libmemcached --cflags`

    PHP_EVAL_LIBLINE($PHP_MEMC_LITE_LIBS, MEMC_LITE_SHARED_LIBADD)
    PHP_EVAL_INCLINE($PHP_MEMC_LITE_CFLAGS)
  else
    AC_MSG_ERROR(Unable to find libmemcached installation)
  fi

  ORIG_CFLAGS="$CFLAGS"
  ORIG_LIBS="$LIBS"
  
  CFLAGS="$CFLAGS $PHP_MEMC_LITE_CFLAGS"
  LIBS="$LIBS $PHP_MEMC_LITE_LIBS"
  
  AC_CACHE_CHECK([whether memcached_instance_st is defined], ac_cv_have_memcached_instance_st, [
    AC_TRY_COMPILE(
      [ #include <libmemcached/memcached.h> ],
      [ const memcached_instance_st *instance = NULL; ],
      [ ac_cv_have_memcached_instance_st="yes" ],
      [ ac_cv_have_memcached_instance_st="no" ]
    )
  ])

  if test "$ac_cv_have_memcached_instance_st" = "yes"; then
     AC_DEFINE(HAVE_LIBMEMCACHED_INSTANCE_ST, [1], [Whether memcached_instance_st is defined])
  fi

  AC_CACHE_CHECK([whether MEMCACHED_DISTRIBUTION_VIRTUAL_BUCKET is defined], ac_cv_have_libmemcached_vbucket, [
    AC_TRY_COMPILE(
      [ #include <libmemcached/memcached.h> ],
      [ MEMCACHED_DISTRIBUTION_VIRTUAL_BUCKET; ],
      [ ac_cv_have_libmemcached_vbucket="yes" ],
      [ ac_cv_have_libmemcached_vbucket="no" ]
    )
  ])

  if test "$ac_cv_have_libmemcached_vbucket" = "yes"; then
     AC_DEFINE(HAVE_LIBMEMCACHED_VBUCKET, [1], [Whether MEMCACHED_DISTRIBUTION_VIRTUAL_BUCKET is defined])
  fi

  AC_CACHE_CHECK([whether memcached_exist is defined], ac_cv_have_memcached_exist, [
    AC_TRY_LINK(
      [ #include <libmemcached/memcached.h> ],
      [ memcached_exist (NULL, NULL, 0); ],
      [ ac_cv_have_memcached_exist="yes" ],
      [ ac_cv_have_memcached_exist="no" ]
    )
  ])

  if test "$ac_cv_have_memcached_exist" = "yes"; then
     AC_DEFINE(HAVE_MEMCACHED_EXIST, [1], [Whether memcached_exist is defined])
  fi
  
  AC_CACHE_CHECK([whether memcached_touch is defined], ac_cv_have_memcached_touch, [
    AC_TRY_LINK(
      [ #include <libmemcached/memcached.h> ],
      [ memcached_touch (NULL, NULL, 0); ],
      [ ac_cv_have_memcached_touch="yes" ],
      [ ac_cv_have_memcached_touch="no" ]
    )
  ])

  if test "$ac_cv_have_memcached_touch" = "yes"; then
     AC_DEFINE(HAVE_MEMCACHED_TOUCH, [1], [Whether memcached_touch is defined])
  fi

  CFLAGS="$ORIG_CFLAGS"
  LIBS="$ORIG_LIBS"

  PHP_CHECK_FUNC(strtoull)
  if test "$ac_cv_func_strtoull" != "yes"; then
    AC_MSG_ERROR(strtoull not found)
  fi

  PHP_ADD_BUILD_DIR($ext_builddir/fastlz, 1)
  PHP_SUBST(MEMC_LITE_SHARED_LIBADD)

  PHP_NEW_EXTENSION(memc_lite, memc_lite.c fastlz/fastlz.c, $ext_shared,,$PHP_MEMC_LITE_CFLAGS)
fi

