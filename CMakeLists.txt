cmake_minimum_required(VERSION 3.25)
project(aprx VERSION 2.9.1 LANGUAGES C)

# Forzar estándar C11 y alertas estrictas para GCC en Debian 13
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Se añade -Wno-stringop-overflow para mitigar la alerta estricta de GCC 14 en aprsis.c
add_compile_options(-Wall -Wextra -Wimplicit-function-declaration -Wno-stringop-overflow -O2)

# Buscar dependencias críticas
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# DEFINICIONES CRÍTICAS DEL PREPROCESADOR (Resuelve los errores de aprx.c)
add_compile_definitions(
    VARRUN="/var/run"
    APRXVERSION="${PROJECT_VERSION}"
)

# Listar todos los archivos fuente del proyecto
set(SOURCES
    agwpesocket.c aprsis.c aprx.c aprx-stat.c aprxpolls.c ax25.c
    beacon.c cellmalloc.c config.c crc.c digipeater.c dprsgw.c
    dupecheck.c erlang.c filter.c historydb.c hlog.c igate.c
    interface.c keyhash.c kiss.c netax25.c netresolver.c
    parse_aprs.c pbuf.c ssl.c telemetry.c timercmp.c
    timestamp.c ttyreader.c valgrind.c
)

# Definir el ejecutable principal del demonio
add_executable(aprx ${SOURCES})

# Asegurar que se incluyan los directorios de OpenSSL y la raíz para config.h
target_include_directories(aprx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OPENSSL_INCLUDE_DIR})

# Enlazar librerías (OpenSSL, hilos del sistema y librería matemática)
target_link_libraries(aprx PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    m
)

# Reglas de instalación estándar para Debian
install(TARGETS aprx DESTINATION sbin)

# CORRECCIÓN: En CMake, instalar archivos directamente en rutas absolutas como /etc 
# rompe la creación de paquetes (.deb). Se usa el estándar SYSCONFDIR.
include(GNUInstallDirs)
install(FILES aprx.conf.in DESTINATION ${CMAKE_INSTALL_SYSCONFDIR} RENAME aprx.conf)
