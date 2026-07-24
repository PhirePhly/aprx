cmake_minimum_required(VERSION 3.25)
project(aprx VERSION 2.9.1 LANGUAGES C)

# Forzar estándar C11 y alertas estrictas para GCC en Debian 13
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wimplicit-function-declaration -O2)

# Buscar dependencias críticas
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Listar todos los archivos fuente del proyecto
set(SOURCES
    agwpesocket.c aprsis.c aprx.c aprx-stat.c aprxpolls.c ax25.c 
    beacon.c cellmalloc.c config.c crc.c digipeater.c dprsgw.c 
    dupecheck.c erlang.c filter.c historydb.c igate.c 
    interface.c keyhash.c kiss.c netax25.c netresolver.c 
    parse_aprs.c pbuf.c ssl.c telemetry.c timercmp.c 
    timestamp.c ttyreader.c valgrind.c
)

# Definir el ejecutable principal del demonio
add_executable(aprx ${SOURCES})

# Enlazar librerías (OpenSSL, hilos del sistema y librería matemática)
target_link_libraries(aprx PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    Threads::Threads
    m
)

# Reglas de instalación estándar para Debian
install(TARGETS aprx DESTINATION sbin)
install(FILES aprx.conf.in DESTINATION /etc RENAME aprx.conf)
