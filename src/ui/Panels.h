// BionicaEngine - Editor UI Panels with active functions
#pragma once
#include "imgui.h"
#include "../viewport.h"
#include <vector>
#include <memory>
#include <string>
extern ViewportRenderer* g_viewport;
enum class EditorMode {
    Code = 0,
    Model,
    Scene,
    Texturing
};
static EditorMode g_editorMode = EditorMode::Model;
inline const char* modeName(EditorMode m) {
    switch (m) {
        case EditorMode::Code:      return "Код";
        case EditorMode::Model:     return "Модель";
        case EditorMode::Scene:     return "Сцена";
        case EditorMode::Texturing: return "Текстурирование";
    }
    return "?";
}
class IPanel {
public:
    virtual ~IPanel() = default;
    virtual const char* name() const = 0;
    virtual void onRender() = 0;
    bool visible = true;
};
class PanelManager {
public:
    void add(std::unique_ptr<IPanel> panel) {
        panels.push_back(std::move(panel));
    }
    void renderAll() {
        for (auto& p : panels) {
            if (!p->visible) {
                continue;
            }
            if (ImGui::Begin(p->name(), &p->visible)) {
                p->onRender();
            }
            ImGui::End();
        }
    }
    void renderVisibilityMenu() {
        for (auto& p : panels) {
            ImGui::MenuItem(p->name(), nullptr, &p->visible);
        }
    }
    std::vector<std::unique_ptr<IPanel>> panels;
};
class SettingsBarPanel : public IPanel {
public:
    const char* name() const override { return "Панель настроек"; }
    void onRender() override {
        ImGui::TextDisabled("Быстрые настройки проекта");
        ImGui::Separator();
        static bool grid = true;
        static bool snap = false;
        static float gridStep = 1.0f;
        ImGui::Checkbox("Сетка", &grid);
        ImGui::SameLine();
        ImGui::Checkbox("Привязка", &snap);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Шаг сетки", &gridStep, 0.1f, 0.1f, 100.0f, "%.2f");
    }
};
class ToolbarTopPanel : public IPanel {
public:
    const char* name() const override { return "Инструменты"; }
    void onRender() override {
        if (ImGui::BeginTabBar("EditorModeTabs")) {
            const EditorMode modes[4] = {
                EditorMode::Code, EditorMode::Model,
                EditorMode::Scene, EditorMode::Texturing
            };
            for (int i = 0; i < 4; i++) {
                if (ImGui::BeginTabItem(modeName(modes[i]))) {
                    g_editorMode = modes[i];
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        switch (g_editorMode) {
            case EditorMode::Code:
                ImGui::Button("Собрать");   ImGui::SameLine();
                ImGui::Button("Запустить"); ImGui::SameLine();
                ImGui::Button("Отладка");
                break;
            case EditorMode::Model:
                if (ImGui::Button("Добавить кость")) {
                    if (g_viewport) {
                        g_viewport->addBone();
                    }
                }
                ImGui::SameLine();
                ImGui::Button("Эскиз");     ImGui::SameLine();
                ImGui::Button("Выдавить");  ImGui::SameLine();
                ImGui::Button("Вращение");
                break;
            case EditorMode::Scene:
                ImGui::Button("Объект");    ImGui::SameLine();
                ImGui::Button("Свет");      ImGui::SameLine();
                ImGui::Button("Камера");
                break;
            case EditorMode::Texturing:
                ImGui::Button("Кисть");     ImGui::SameLine();
                ImGui::Button("Материал");  ImGui::SameLine();
                ImGui::Button("UV");
                break;
        }
    }
};
class ToolPropertiesPanel : public IPanel {
public:
    const char* name() const override { return "Свойства инструмента"; }
    void onRender() override {
        ImGui::Text("Режим: %s", modeName(g_editorMode));
        ImGui::Separator();
        if (g_editorMode == EditorMode::Model) {
            size_t boneCount = g_viewport ? g_viewport->bones.size() : 0;
            ImGui::Text("Скелетная структура:");
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Активных костей: %zu", boneCount);
            if (boneCount > 0) {
                ImGui::Separator();
                ImGui::TextDisabled("Последняя кость:");
                auto& lastBone = g_viewport->bones.back();
                ImGui::Text("Head: (%.1f, %.1f, %.1f)", lastBone.head.x, lastBone.head.y, lastBone.head.z);
                ImGui::Text("Tail: (%.1f, %.1f, %.1f)", lastBone.tail.x, lastBone.tail.y, lastBone.tail.z);
            }
            ImGui::Separator();
            static float depth = 10.0f;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##depth", &depth, 0.5f, 0.0f, 1000.0f, "Выдавливание %.1f м");
            if (ImGui::Button("Применить операцию")) {
            }
        } else {
            ImGui::TextWrapped("Здесь появятся параметры выбранного инструмента.");
        }
    }
};
class ViewportPanel : public IPanel {
public:
    const char* name() const override { return "Рабочая область"; }
    void onRender() override {
        if (g_viewport == nullptr) {
            ImGui::TextDisabled("Viewport не инициализирован");
            return;
        }
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 8.0f) avail.x = 8.0f;
        if (avail.y < 8.0f) avail.y = 8.0f;
        uint32_t w = (uint32_t)avail.x;
        uint32_t h = (uint32_t)avail.y;
        if (w != g_viewport->width() || h != g_viewport->height()) {
            g_viewport->resize(w, h);
        }
        ImGui::Image((ImTextureID)(uintptr_t)g_viewport->textureId(), avail);
        if (ImGui::IsItemHovered()) {
            g_viewport->camera.handleInput();
        }
        ImVec2 origin = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        char info[128];
        snprintf(info, sizeof(info), "%u x %u   dist %.1f   bones: %zu",
                 g_viewport->width(), g_viewport->height(),
                 g_viewport->camera.distance, g_viewport->bones.size());
        dl->AddText(ImVec2(origin.x + 10.0f, origin.y + 8.0f),
                    IM_COL32(150, 160, 175, 255), info);
    }
};
class ProjectTreePanel : public IPanel {
public:
    const char* name() const override { return "Проект"; }
    void onRender() override {
        if (ImGui::TreeNodeEx("BionicaProject", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("src", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TreeNodeEx("main.cpp",
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("assets")) {
                ImGui::TreeNodeEx("cube.obj",
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreePop();
            }
            ImGui::TreeNodeEx("CMakeLists.txt",
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreePop();
        }
    }
};
class QuickAccessBarPanel : public IPanel {
public:
    const char* name() const override { return "Быстрый доступ"; }
    void onRender() override {
        ImGui::Button("Сохранить (Ctrl+S)"); ImGui::SameLine();
        ImGui::Button("Отменить (Ctrl+Z)");  ImGui::SameLine();
        ImGui::Button("Повторить (Ctrl+Y)"); ImGui::SameLine();
        ImGui::Button("Кадрировать (F)");    ImGui::SameLine();
        ImGui::TextDisabled(" | FPS: %.1f", ImGui::GetIO().Framerate);
    }
};