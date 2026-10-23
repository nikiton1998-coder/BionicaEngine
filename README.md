# BionicaEngine

Экспериментальный игровой движок и редактор **Bionica Editor** на C++20 и Vulkan.

**Статус:** ранняя разработка. Работают: Vulkan-рендер, докируемый интерфейс на Dear ImGui, 3D-viewport с орбитальной камерой, редактирование костей.

## Идея

- Моделирование от костей: послойные SDF-поля поверх скелета.
- Инженерные эскизы и операции в стиле КОМПАС-3D.
- Опциональные воксели для разрушений.
- Человекочитаемый скриптовый язык.
- Метрическая система: 1 единица = 1 метр.

## Требования (Windows)

- Windows 10/11 и GPU с поддержкой Vulkan.
- Visual Studio с компонентом «Разработка классических приложений на C++» (MSVC x64).
- CMake 3.25+.
- Vulkan SDK с `glslc`. Переменная окружения `VULKAN_SDK` должна указывать на установку SDK.
- vcpkg: `glfw3`, `glm`, `imgui[core,glfw-binding,vulkan-binding,docking-experimental]`.

## Сборка

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\BionicaEditor.exe
```

Если CMake не находит пакеты vcpkg, добавьте к первой команде `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`.

## Управление в «Рабочей области»

- ЛКМ: вращение камеры.
- СКМ: панорама.
- Колесо: приближение и отдаление.

## Структура

```
src/main.cpp     Vulkan, ImGui, главный цикл
src/viewport.h   3D-viewport, камера, кости
src/ui/Panels.h  панели интерфейса
shaders/         GLSL-шейдеры, компилируются в SPIR-V при сборке
```

## Известные ограничения

- Пока только Windows: путь к шрифту Segoe UI задан жёстко.
- Лицензия не выбрана.