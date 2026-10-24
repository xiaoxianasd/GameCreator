cmake_minimum_required(VERSION 3.19)

project(GameCreator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(GC_CORE_SOURCES
    src/core/AssetSpec.h
    src/core/Common.h
    src/core/ReviewReport.h
    src/models/IImageModel.h
    src/models/MockAdapter.h
    src/models/MockAdapter.cpp
    src/models/OpenAiCompatAdapter.h
    src/models/OpenAiCompatAdapter.cpp
    src/models/ComfyUiAdapter.h
    src/models/ComfyUiAdapter.cpp
    src/models/ModelRouter.h
    src/models/PromptPolicy.h
    src/factory/AssetFactory.h
    src/factory/AssetFactory.cpp
    src/checks/DeterministicChecks.h
    src/checks/VisualHeuristics.h
    src/export/AssetExporter.h
    src/export/AssetExporter.cpp
    src/storage/ProjectStore.h
    src/storage/ProjectStore.cpp
    src/orchestrator/TaskOrchestrator.h
    src/orchestrator/TaskOrchestrator.cpp
)

add_executable(GameCreator
    src/main.cpp
    ${GC_CORE_SOURCES}
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ExportDialog.h
    src/ui/ExportDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/GlassPanel.h
    src/ui/GlassPanel.cpp
    src/ui/WallpaperBackground.h
    src/ui/WallpaperBackground.cpp
)

target_include_directories(GameCreator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(GameCreator PRIVATE Qt6::Widgets Qt6::Network)

# 无 GUI 流水线自测（mock 模式端到端验证）
add_executable(PipelineTest
    tests/pipeline_test.cpp
    ${GC_CORE_SOURCES}
)
target_include_directories(PipelineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PipelineTest PRIVATE Qt6::Gui Qt6::Network)

# Windows 下仍保留控制台输出，方便查看运行日志
if (WIN32)
    set_target_properties(GameCreator PROPERTIES WIN32_EXECUTABLE OFF)

    # 构建后自动用 windeployqt 部署 Qt 运行时（DLL + 平台插件）到 exe 旁
    get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${QT_BIN_DIR}")
    if (WINDEPLOYQT_EXE)
        add_custom_command(TARGET GameCreator POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --release --no-translations
                    "$<TARGET_FILE:GameCreator>"
            COMMENT "Deploying Qt runtime next to GameCreator.exe")
    else ()
        message(WARNING "windeployqt not found: 运行前请手动部署 Qt 运行时或将 Qt bin 加入 PATH")
    endif ()
endif()

# 安装阶段把示例配置与工作流模板一并拷到构建目录
set(_aux_files
    config.example.json
    workflows/comfyui_example.json
)
foreach (_f ${_aux_files})
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
        add_custom_command(TARGET GameCreator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
                    "$<TARGET_FILE_DIR:GameCreator>/${_f}")
    endif ()
endforeach ()
