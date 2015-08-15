AC_PREREQ([2.68])
AC_INIT([httprcclient], [0.1], [httprcclient@fln.lt])
AM_INIT_AUTOMAKE([-Wall foreign check-news silent-rules -Werror])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([src/config.h])

AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_LIB([curl], [curl_easy_init], ,[AC_MSG_ERROR([Please install libcurl])])
AC_CHECK_LIB([jansson], [json_loadb], ,[AC_MSG_ERROR([Please install libjansson])])

AC_CHECK_HEADER([jansson.h], ,[AC_MSG_ERROR([jansson.h is missing])])
AC_CHECK_HEADER([curl/curl.h], ,[AC_MSG_ERROR([curl.h is missing])])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
