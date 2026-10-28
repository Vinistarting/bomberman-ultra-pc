# Launcher do Bomberman ULTRA (Win32 puro, sem dependências externas).
# Pode ser compilado sozinho:
#   cmake -S launcher -B build-launcher && cmake --build build-launcher --config Release
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED PROJECT_NAME)
    project(BombermanUltraLauncher LANGUAGES CXX RC)
else()
    enable_language(RC)
endif()

# Ícone: o do jogo (bomberman.ico, gerado do ICON0.PNG do usuário pelo build)
# ou, sem ele (pacote de release, CI), o genérico desenhado pelo make_icon.py.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bomberman.ico" AND NOT BMU_GENERIC_ICON)
    set(_bmu_icon "${CMAKE_CURRENT_SOURCE_DIR}/bomberman.ico")
else()
    set(_bmu_icon "${CMAKE_CURRENT_SOURCE_DIR}/generic.ico")
endif()
configure_file("${_bmu_icon}" "${CMAKE_CURRENT_BINARY_DIR}/launcher_icon.ico" COPYONLY)

add_executable(BombermanUltraLauncher WIN32
    launcher.cpp
    launcher.rc
)
target_compile_features(BombermanUltraLauncher PRIVATE cxx_std_17)
target_include_directories(BombermanUltraLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(BombermanUltraLauncher PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(BombermanUltraLauncher PRIVATE comctl32 gdiplus shell32 ole32 uuid uxtheme comdlg32 bcrypt advapi32 urlmon wininet)
if(MSVC)
    # O manifest vem do launcher.rc; não gerar outro.
    target_link_options(BombermanUltraLauncher PRIVATE /MANIFEST:NO)
    target_compile_options(BombermanUltraLauncher PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    set_property(TARGET BombermanUltraLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif(MINGW)
    target_link_options(BombermanUltraLauncher PRIVATE -municode -static)
endif()

# Copia o launcher para a raiz do port (ao lado de game/), onde ele procura os arquivos.
get_filename_component(BMU_PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_custom_command(TARGET BombermanUltraLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:BombermanUltraLauncher>" "${BMU_PORT_ROOT}/BombermanUltraLauncher.exe"
    COMMENT "Copiando o launcher para ${BMU_PORT_ROOT}")
