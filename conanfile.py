import os
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.cmake import cmake_layout

class BrocsegRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    default_options = {
        "glad*:gl_profile": "core",
        "glad*:gl_version": "4.6",
        "glad*:spec": "gl",
        "glad*:no_loader": False
    }

    def requirements(self):
        self.requires("sdl/2.30.7")
        self.requires("glew/2.2.0")
        self.requires("glad/0.1.36")
        self.requires("glm/cci.20230113")
        self.requires("assimp/5.4.2")
        self.requires("imgui/1.91.0")
        self.requires("openmesh/11.0")

    def generate(self):
        copy(self, "*sdl*", os.path.join(self.dependencies["imgui"].package_folder,
            "res", "bindings"), os.path.join(self.source_folder, "src", "imgui_bindings"))
        copy(self, "*opengl3*", os.path.join(self.dependencies["imgui"].package_folder,
            "res", "bindings"), os.path.join(self.source_folder, "src", "imgui_bindings"))
    #def layout(self):
        #cmake_layout(self)

