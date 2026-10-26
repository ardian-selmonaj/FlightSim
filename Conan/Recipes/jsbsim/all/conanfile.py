import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, copy, export_conandata_patches, get, rmdir

required_conan_version = ">=2"


class JsbsimConan(ConanFile):
    """JSBSim built from source, adapted from the ConanCenter recipe (which only offers 1.1.13 and 1.3.1).
    FlightSim uses the version of the jsbsim Python package that the HHMARL policies were trained with,
    so that JSBSim behaves the same in C++ and in Python. JSBSim's bundled expat is used, because the
    system expat handling of the 1.3.0 CMake files differs from later versions."""

    name = "jsbsim"
    description = (
        "JSBSim is a multi-platform, general purpose object-oriented "
        "Flight Dynamics Model (FDM) written in C++"
    )
    license = "LGPL-2.1-or-later"
    topics = ("aircraft", "aerospace", "flight-dynamics", "flight-simulation")
    homepage = "https://github.com/JSBSim-Team/jsbsim"
    url = "https://github.com/conan-io/conan-center-index"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def export_sources(self):
        export_conandata_patches(self)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def validate(self):
        check_min_cppstd(self, "11")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        apply_conandata_patches(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["SYSTEM_EXPAT"] = False
        tc.variables["BUILD_DOCS"] = False
        tc.variables["BUILD_PYTHON_MODULE"] = False
        # Neither the pkg-config file nor the unit tests are needed
        tc.cache_variables["CMAKE_DISABLE_FIND_PACKAGE_PkgConfig"] = True
        tc.cache_variables["CMAKE_DISABLE_FIND_PACKAGE_CxxTest"] = True
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "COPYING", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))

    def package_info(self):
        self.cpp_info.set_property("pkg_config_name", "JSBSim")
        self.cpp_info.libs = ["JSBSim"]
        self.cpp_info.includedirs = [os.path.join("include", "JSBSim")]
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs.append("m")
        elif self.settings.os == "Windows":
            self.cpp_info.system_libs.extend(["wsock32", "ws2_32"])
            if not self.options.shared:
                self.cpp_info.defines.append("JSBSIM_STATIC_LINK")
