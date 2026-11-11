#include "renderer/Renderer.h"
#include "animation/SkinnedMesh.h"
#include "core/Log.h"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace MipsyncEngine;
static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    GLFWwindow* window = nullptr;
    try {
        Log::Init();
        Require(glfwInit(), "GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(64, 64, "Unlit rendering test", nullptr, nullptr);
        Require(window != nullptr, "Hidden OpenGL context unavailable");
        glfwMakeContextCurrent(window);
        Require(gladLoadGL((GLADloadfunc)glfwGetProcAddress), "OpenGL loader failed");
        {
            Renderer renderer;
            renderer.Init();
            renderer.ResizeOutput(64, 64);
            auto& settings = renderer.GetPS1Settings();
            settings.fogEnabled = false;
            settings.colorDepthLimit = settings.ditheringEnabled = false;
            Camera camera;
            camera.SetPosition({0, 0, 0});
            camera.SetRotation(-90, 0);
            camera.SetPerspective(60, 1, .1f, 20);
            std::vector<Vertex> vertices = {
                {{-1,-1,-2}, {0,0,1}, {.5f,.5f}, {1,1,1,1}},
                {{ 1,-1,-2}, {0,0,1}, {.5f,.5f}, {1,1,1,1}},
                {{ 0, 1,-2}, {0,0,1}, {.5f,.5f}, {1,1,1,1}}
            };
            Mesh mesh(vertices, {0,1,2});
            std::vector<SkinnedVertex> skinnedVertices;
            for (const auto& v : vertices)
                skinnedVertices.push_back({v.position,v.normal,v.uv,v.color,{0,0,0,0},{1,0,0,0}});
            SkinnedMesh skin(skinnedVertices, {0,1,2});
            const glm::mat4 identity(1);
            const glm::vec4 material(.75f,.25f,.125f,1);
            SceneLightGpu red{};
            red.dirRange = {0,0,-1,10};
            red.colorIntensity = {1,0,0,1};
            SceneLightGpu green = red;
            green.colorIntensity = {0,1,0,1};
            auto draw = [&](bool skinned) {
                if (skinned)
                    renderer.DrawSkinnedMesh(skin, identity, identity, glm::vec3(0), 1,
                                             &identity, 1, nullptr, material);
                else
                    renderer.DrawMesh(mesh, identity, nullptr, material);
            };
            auto pixel = [&] {
                std::array<unsigned char,4> rgba{};
                glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
                Require(glGetError() == GL_NO_ERROR, "OpenGL rendering error");
                return rgba;
            };
            auto render = [&](bool skinned, bool unlit, const SceneLightGpu& light) {
                renderer.SetSceneLights(&light,1);
                renderer.BeginScene(camera);
                renderer.SetObjectUnlit(unlit);
                for (int i=0; i<3; ++i) draw(skinned);
                Require(renderer.GetStatistics().lightingUploads == (unlit ? 0u : 1u),
                        "Unlit must upload no lights; repeated Lit draws must upload once");
                auto result = pixel();
                renderer.EndScene();
                return result;
            };
            for (bool skinned : {false,true}) {
                settings.fogStart = 10; settings.fogEnd = 40;
                const auto first = render(skinned,true,red);
                Require(first[0] > 180 && first[1] > 55 && first[2] > 25,
                        "Unlit must retain its material color");
                Require(render(skinned,true,green) == first, "Unlit must ignore light changes");
                const auto lit = render(skinned,false,red);
                Require(lit != first && lit[0] > lit[1], "Lit must receive the current light after Unlit");
                Require(render(skinned,true,red) == first, "Lit to Unlit must restore material color");
                settings.fogEnabled = true;
                settings.fogStart = .5f; settings.fogEnd = 4;
                Require(render(skinned,true,red) == first, "Optimization must preserve the existing Unlit fog behavior");
                const auto fogged = render(skinned,false,red);
                Require(fogged != lit, "Lit fog must remain active");
                settings.fogEnabled = false;
                settings.colorGradingEnabled = true;
                settings.colorFilter = {.5f,1,1};
                Require(render(skinned,true,red) != first, "Unlit must retain color grading");
                settings.colorGradingEnabled = false;
            }
            renderer.SetSceneLights(&red,1);
            renderer.BeginScene(camera);
            renderer.SetObjectUnlit(true);
            draw(false); draw(true);
            Require(renderer.GetStatistics().lightingUploads == 0, "Mixed Unlit meshes must upload no lights");
            renderer.SetObjectUnlit(false);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(false); draw(true); draw(false); draw(true);
            const auto redPixel = pixel();
            Require(renderer.GetStatistics().lightingUploads == 2, "Mixed Lit shaders must upload once each");
            renderer.SetSceneLights(&green,1);
            glClear(GL_DEPTH_BUFFER_BIT);
            draw(false); draw(true);
            Require(pixel() != redPixel, "Mid-pass lights must change the rendered pixels");
            Require(renderer.GetStatistics().lightingUploads == 4, "Mid-pass light changes must invalidate both shaders");
            renderer.EndScene();
            const auto beforeUpdate = render(true, true, red);
            auto movedVertices = skinnedVertices;
            for (auto& vertex : movedVertices) vertex.position.x += 10.0f;
            skin.UpdateVertexData(movedVertices);
            Require(render(true, true, red) != beforeUpdate,
                    "Updating a posed preview must change uploaded GPU vertices");
            skin.UpdateVertexData(skinnedVertices);
            Require(render(true, true, red) == beforeUpdate && skin.GetIndexCount() == 3,
                    "Updating a preview must preserve topology and restore the pose");
            namespace fs = std::filesystem;
            const auto project = fs::absolute("Testing/project-renderer-pixels");
            fs::create_directories(project / "renderer/pc");
            auto shader = [&](const char* name, const char* source) {
                std::ofstream(project / "renderer/pc" / name, std::ios::binary) << source;
            };
            const char* redShader = "#version 330 core\nout vec4 FragColor; void main(){FragColor=vec4(1,0,0,1);}";
            const char* greenShader = "#version 330 core\nout vec4 FragColor; void main(){FragColor=vec4(0,1,0,1);}";
            shader("mesh.frag", redShader);
            renderer.SetProjectRoot(project, true);
            Require(renderer.ReloadProjectShaders(), "Valid project shader did not compile");
            const auto customRed = render(false, true, red);
            Require(customRed[0] == 255 && customRed[1] == 0, "Custom fragment did not change actual pixels");
            Require(render(true, true, red) == customRed, "Custom fragment did not reach skinned mesh");
            shader("mesh.frag", "invalid GLSL");
            Require(!renderer.ReloadProjectShaders(), "Invalid project shader accepted");
            Require(render(false, true, red) == customRed, "Shader failure lost the last valid rendering");
            shader("mesh.frag", greenShader);
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            const auto customGreen = render(false, true, red);
            Require(customGreen[0] == 0 && customGreen[1] == 255, "Corrected shader not applied");
            shader("screen.frag", redShader);
            Require(renderer.ReloadProjectShaders(), "Custom screen pass did not compile");
            render(false, true, red);
            renderer.GetRenderTarget(nullptr).Bind();
            Require(pixel() == customRed, "Screen shader did not process final scene pixels");
            renderer.GetRenderTarget(nullptr).Unbind();
            fs::remove(project / "renderer/pc/mesh.frag");
            fs::remove(project / "renderer/pc/screen.frag");
            Require(renderer.ReloadProjectShaders(), "Deleted overrides did not restore defaults");
            Require(render(false, true, red) != customGreen, "Deleted shader remains active");
            std::cout << "Unlit mesh/skinned pixels, fog, Lit toggles and lazy light uploads passed\n";
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
