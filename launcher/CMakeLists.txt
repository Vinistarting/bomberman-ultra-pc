# Launcher do Bomberman ULTRA (Win32 puro, sem dependências externas).
# Pode ser compilado sozinho:
#   cmake -S launcher -B build-launcher && cmake --build build-launcher --config Release
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED PROJECT_NAME)
    project(BombermanUltraLauncher LANGUAGES CXX RC)
else()
    enable_language(RC)
endif()

add_executable(BombermanUltraLauncher WIN32
    launcher.cpp
    launcher.rc
)
target_compile_features(BombermanUltraLauncher PRIVATE cxx_std_17)
target_compile_definitions(BombermanUltraLauncher PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(BombermanUltraLauncher PRIVATE comctl32 gdiplus shell32 ole32 uxtheme comdlg32 bcrypt advapi32)
if(MSVC)
    # O manifest vem do launcher.rc; não gerar outro.
    target_link_options(BombermanUltraLauncher PRIVATE /MANIFEST:NO)
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
