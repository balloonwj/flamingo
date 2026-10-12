# 在 cmake 配置阶段从源码编译第三方库的辅助函数（Linux），由 CMakeLists.txt include。
#
# 设计见 docs/服务端依赖库源码编译方案.md 第 11.2 节：配置阶段用 execute_process 依次运行库自带的
# configure、make -j<CPU 核数>、make install，装进构建目录下的 deps-install/<库名>/，之后 CMakeLists
# 按完整路径链接装好的静态库。之所以在配置阶段而不是 make 阶段编译，是因为 MariaDB 连接器在配置阶段
# 就要用到 OpenSSL 的头文件与库（上游 CMakeLists.txt 的 FIND_PACKAGE(OpenSSL) 与 TRY_RUN）。
# 安装目录里有一个标记文件，记录版本号、源码目录、编译器与编译参数；这些都没变且产物还在时跳过编译，
# 所以只有第一次运行 cmake（或换了版本、参数，或删掉了构建目录）时才会编译。
#
# 典型用法（CMakeLists.txt）：
#     include(${PROJECT_SOURCE_DIR}/cmake/FlamingoDeps.cmake)
#     flamingo_build_libsodium(${PROJECT_SOURCE_DIR}/libsodium-1.0.22 1.0.22)
#     include_directories(${LIBSODIUM_INCLUDE_DIR})
#     TARGET_LINK_LIBRARIES(fileserver ... ${LIBSODIUM_LIB})
#     flamingo_check_perl_for_openssl()
#     flamingo_build_openssl(${PROJECT_SOURCE_DIR}/openssl-3.5.9 3.5.9)
#     （再把 FLAMINGO_OPENSSL_INCLUDE_DIR 等交给 find_package(OpenSSL)，见 CMakeLists.txt）
#
# balloonwj@qq.com   2026-10-10

include(ProcessorCount)

# 编译第三方库时 make 的并行任务数：取 CPU 核数，取不到时为 1。与用户运行 make 时是否带 -j 无关
ProcessorCount(FLAMINGO_DEPS_JOBS)
if (FLAMINGO_DEPS_JOBS EQUAL 0)
    set(FLAMINGO_DEPS_JOBS 1)
endif()

# 各库构建目录的上级目录（每个库一个子目录，编译日志也放在这里）
set(FLAMINGO_DEPS_BUILD_ROOT ${PROJECT_BINARY_DIR}/deps-build)
# 各库安装目录的上级目录（每个库一个子目录，含 include/ 与 lib/）
set(FLAMINGO_DEPS_INSTALL_ROOT ${PROJECT_BINARY_DIR}/deps-install)
# 标记文件的文件名，放在各库的安装目录里
set(FLAMINGO_DEPS_STAMP_NAME flamingo-build-stamp.txt)
# 编译失败时在 cmake 输出里显示的日志末尾字节数；完整内容见日志文件
set(FLAMINGO_DEPS_LOG_TAIL_BYTES 4000)

# 第三方库的 configure 生成的是 GNU make 的 Makefile，与 CMake 用哪种生成器无关，所以单独找 make
find_program(FLAMINGO_DEPS_MAKE NAMES gmake make)
if (NOT FLAMINGO_DEPS_MAKE)
    message(FATAL_ERROR "make not found: it is needed to build the bundled libsodium / OpenSSL sources")
endif()

# 执行一条编译命令，标准输出与标准错误都写进 <构建根目录>/<库名>-<步骤名>.log；
# 命令失败时在 cmake 输出里打印日志末尾，并以 FATAL_ERROR 终止配置。
#   flamingo_dep_step(<库名> <步骤名> <工作目录> <命令及参数…>)
function(flamingo_dep_step dep step workdir)
    set(log ${FLAMINGO_DEPS_BUILD_ROOT}/${dep}-${step}.log)
    execute_process(COMMAND ${ARGN}
                    WORKING_DIRECTORY ${workdir}
                    OUTPUT_FILE ${log}
                    ERROR_FILE ${log}
                    RESULT_VARIABLE rc)
    if (NOT rc EQUAL 0)
        file(READ ${log} content)
        string(LENGTH "${content}" len)
        if (len GREATER FLAMINGO_DEPS_LOG_TAIL_BYTES)
            math(EXPR start "${len} - ${FLAMINGO_DEPS_LOG_TAIL_BYTES}")
            string(SUBSTRING "${content}" ${start} -1 content)
        endif()
        message(FATAL_ERROR "${dep}: '${step}' failed (exit ${rc}), full log: ${log}\n...\n${content}")
    endif()
endfunction()

# 判断某个库是否已按同样的参数编译安装过：标记文件存在、内容与 stamp 完全相同、
# 且列出的产物文件都存在时，把 out_var 设为 TRUE，否则为 FALSE。
#   flamingo_dep_is_current(<库名> <标记内容> <输出变量名> <产物文件…>)
function(flamingo_dep_is_current dep stamp out_var)
    set(result FALSE)
    set(stamp_file ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep}/${FLAMINGO_DEPS_STAMP_NAME})
    if (EXISTS ${stamp_file})
        file(READ ${stamp_file} old_stamp)
        if (old_stamp STREQUAL stamp)
            set(result TRUE)
            foreach (product ${ARGN})
                if (NOT EXISTS ${product})
                    set(result FALSE)
                endif()
            endforeach()
        endif()
    endif()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# 编译某个库之前清空它的构建目录与安装目录（上一次的残留可能来自别的版本或参数）
#   flamingo_dep_reset(<库名>)
function(flamingo_dep_reset dep)
    file(REMOVE_RECURSE ${FLAMINGO_DEPS_BUILD_ROOT}/${dep} ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep})
    file(MAKE_DIRECTORY ${FLAMINGO_DEPS_BUILD_ROOT}/${dep} ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep})
endfunction()

# 编译安装成功后写标记文件。必须放在最后一步之后：中途失败时没有标记文件，下次 cmake 会整个重编
#   flamingo_dep_write_stamp(<库名> <标记内容>)
function(flamingo_dep_write_stamp dep stamp)
    file(WRITE ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep}/${FLAMINGO_DEPS_STAMP_NAME} "${stamp}")
endfunction()

# 在配置阶段编译 libsodium 的静态库（autotools：sh configure、make、make install）。
# 仓库里的 configure 权限位是 644，所以用 sh 调用；它调用 config.guess 等辅助脚本时也都经过 $SHELL。
# configure 的 C 编译器与 CMake 用的相同（CC=${CMAKE_C_COMPILER}）；编译选项用 libsodium 自己的默认值
# （带优化），与我们的程序是 Debug 还是 Release 无关。
#   flamingo_build_libsodium(<源码目录> <版本号>)
# 在调用方作用域里设置：
#   LIBSODIUM_INCLUDE_DIR  头文件目录（安装目录下的 include）
#   LIBSODIUM_LIB          静态库 libsodium.a 的完整路径
function(flamingo_build_libsodium source_dir version)
    set(dep libsodium)
    set(build_dir ${FLAMINGO_DEPS_BUILD_ROOT}/${dep})
    set(prefix ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep})
    set(lib ${prefix}/lib/libsodium.a)
    # --with-pic：静态库要链进可执行文件，编成位置无关代码，开启 PIE 的编译器也能链接；
    # --disable-dependency-tracking：只编一次，不需要生成依赖关系文件
    set(args --enable-static --disable-shared --with-pic --disable-dependency-tracking)
    set(stamp "libsodium ${version}\nsource=${source_dir}\nCC=${CMAKE_C_COMPILER}\nargs=${args}\n")

    if (NOT EXISTS ${source_dir}/configure)
        message(FATAL_ERROR "libsodium source not found: ${source_dir}/configure")
    endif()

    flamingo_dep_is_current(${dep} "${stamp}" current ${lib} ${prefix}/include/sodium.h)
    if (current)
        message(STATUS "libsodium ${version}: already built from source, skipped (${prefix})")
    else()
        message(STATUS "libsodium ${version}: building from source with ${FLAMINGO_DEPS_JOBS} jobs ...")
        flamingo_dep_reset(${dep})
        flamingo_dep_step(${dep} configure ${build_dir}
                          ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER}
                          sh ${source_dir}/configure ${args} --prefix=${prefix})
        flamingo_dep_step(${dep} make ${build_dir} ${FLAMINGO_DEPS_MAKE} -j${FLAMINGO_DEPS_JOBS})
        flamingo_dep_step(${dep} install ${build_dir} ${FLAMINGO_DEPS_MAKE} install)
        flamingo_dep_write_stamp(${dep} "${stamp}")
        message(STATUS "libsodium ${version}: built (${lib})")
    endif()

    set(LIBSODIUM_INCLUDE_DIR ${prefix}/include PARENT_SCOPE)
    set(LIBSODIUM_LIB ${lib} PARENT_SCOPE)
endfunction()

# 自带 OpenSSL 的构建脚本（Configure 及 make 时调用的生成脚本）需要的 Perl 模块。清单在只装最基本 Perl 的
# 容器里逐个实测得出（2026-10-12，docs/服务端依赖库源码编译方案.md 第 11.2 节）：CentOS 7 只装 perl 包时缺
# IPC::Cmd、Time::Piece；Rocky Linux 9 只装 perl-interpreter 时缺全部六个。Debian / Ubuntu 的 perl 包已全部包含
set(FLAMINGO_OPENSSL_PERL_MODULES FindBin lib IPC::Cmd File::Compare File::Copy Time::Piece)

# 缺少 Perl 或模块时给出的安装提示（cmake 的输出，用英文）。三条命令都在对应的容器里实测过：
# CentOS 7 装这三个包后、Rocky 9 装完整的 perl 元包后、Ubuntu 24.04 装 perl 包后，上面六个模块都在
string(REPLACE ";" " " FLAMINGO_OPENSSL_PERL_MODULES_TEXT "${FLAMINGO_OPENSSL_PERL_MODULES}")
set(FLAMINGO_OPENSSL_PERL_HINT
    "The bundled OpenSSL is built with Perl and needs these Perl modules: ${FLAMINGO_OPENSSL_PERL_MODULES_TEXT}.\n"
    "Install them, then run cmake again:\n"
    "  CentOS / RHEL 7:           yum install perl perl-IPC-Cmd perl-Time-Piece\n"
    "  RHEL / Rocky / Alma 8, 9:  dnf install perl\n"
    "  Debian / Ubuntu:           apt install perl\n"
    "Or configure with -DWITH_SYSTEM_OPENSSL=ON to use the system OpenSSL instead.")

# 检查 Perl 及自带 OpenSSL 的构建脚本需要的模块（FLAMINGO_OPENSSL_PERL_MODULES），缺失时报错并给出安装命令。
# 找到时 find_package(Perl) 把解释器路径写进缓存变量 PERL_EXECUTABLE。
#   flamingo_check_perl_for_openssl()
function(flamingo_check_perl_for_openssl)
    find_package(Perl QUIET)
    if (NOT PERL_FOUND)
        string(REPLACE ";" "" hint "${FLAMINGO_OPENSSL_PERL_HINT}")
        message(FATAL_ERROR "Perl not found.\n${hint}")
    endif()
    set(missing "")
    foreach (module ${FLAMINGO_OPENSSL_PERL_MODULES})
        execute_process(COMMAND ${PERL_EXECUTABLE} -M${module} -e 1
                        RESULT_VARIABLE rc
                        OUTPUT_QUIET
                        ERROR_QUIET)
        if (NOT rc EQUAL 0)
            list(APPEND missing ${module})
        endif()
    endforeach()
    if (missing)
        string(REPLACE ";" " " missing_text "${missing}")
        string(REPLACE ";" "" hint "${FLAMINGO_OPENSSL_PERL_HINT}")
        message(FATAL_ERROR "Perl modules missing: ${missing_text} (perl: ${PERL_EXECUTABLE}).\n${hint}")
    endif()
    message(STATUS "Perl for the bundled OpenSSL: ${PERL_EXECUTABLE}, required modules present")
endfunction()

# 在配置阶段编译 OpenSSL 的静态库（perl Configure、make build_libs、make install_dev）。
# 目标平台由 Configure 自动识别（不写死 linux-x86_64，aarch64 等也能用）；C 编译器与 CMake 用的相同；
# --libdir=lib 是因为 OpenSSL 3 在 64 位 Linux 上默认装到 lib64。
# OPENSSLDIR 有意设成安装目录下的 ssl 子目录：make install_dev 只装头文件、库与 pkgconfig 文件，不会创建它，
# 于是运行时找不到 openssl.cnf，也就不会读到系统里别的版本的配置（CentOS 7 的是 1.0.2 的）；
# 根证书由 HttpsClient 另行探测系统证书包（utils/CaBundleLocator.h）。调用前须先调 flamingo_check_perl_for_openssl。
#   flamingo_build_openssl(<源码目录> <版本号>)
# 在调用方作用域里设置：
#   FLAMINGO_OPENSSL_INCLUDE_DIR  头文件目录
#   FLAMINGO_OPENSSL_SSL_LIB      libssl.a 的完整路径
#   FLAMINGO_OPENSSL_CRYPTO_LIB   libcrypto.a 的完整路径
#   FLAMINGO_OPENSSL_DIR          编进库里的 OPENSSLDIR
function(flamingo_build_openssl source_dir version)
    set(dep openssl)
    set(build_dir ${FLAMINGO_DEPS_BUILD_ROOT}/${dep})
    set(prefix ${FLAMINGO_DEPS_INSTALL_ROOT}/${dep})
    set(ssl_lib ${prefix}/lib/libssl.a)
    set(crypto_lib ${prefix}/lib/libcrypto.a)
    set(openssldir ${prefix}/ssl)
    # no-shared：只要静态库；no-tests、no-docs：不编测试程序、不生成文档（这两部分的源码已从仓库裁掉）
    set(args no-shared no-tests no-docs --libdir=lib --openssldir=${openssldir})
    set(stamp "openssl ${version}\nsource=${source_dir}\nCC=${CMAKE_C_COMPILER}\nperl=${PERL_EXECUTABLE}\nargs=${args}\n")

    if (NOT EXISTS ${source_dir}/Configure)
        message(FATAL_ERROR "OpenSSL source not found: ${source_dir}/Configure")
    endif()

    flamingo_dep_is_current(${dep} "${stamp}" current ${ssl_lib} ${crypto_lib} ${prefix}/include/openssl/opensslv.h)
    if (current)
        message(STATUS "OpenSSL ${version}: already built from source, skipped (${prefix})")
    else()
        message(STATUS "OpenSSL ${version}: building from source with ${FLAMINGO_DEPS_JOBS} jobs, this takes a few minutes ...")
        flamingo_dep_reset(${dep})
        flamingo_dep_step(${dep} configure ${build_dir}
                          ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER}
                          ${PERL_EXECUTABLE} ${source_dir}/Configure ${args} --prefix=${prefix})
        flamingo_dep_step(${dep} make ${build_dir} ${FLAMINGO_DEPS_MAKE} -j${FLAMINGO_DEPS_JOBS} build_libs)
        flamingo_dep_step(${dep} install ${build_dir} ${FLAMINGO_DEPS_MAKE} install_dev)
        flamingo_dep_write_stamp(${dep} "${stamp}")
        message(STATUS "OpenSSL ${version}: built (${prefix}/lib)")
    endif()

    set(FLAMINGO_OPENSSL_INCLUDE_DIR ${prefix}/include PARENT_SCOPE)
    set(FLAMINGO_OPENSSL_SSL_LIB ${ssl_lib} PARENT_SCOPE)
    set(FLAMINGO_OPENSSL_CRYPTO_LIB ${crypto_lib} PARENT_SCOPE)
    set(FLAMINGO_OPENSSL_DIR ${openssldir} PARENT_SCOPE)
endfunction()
