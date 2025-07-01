from setuptools import setup, Extension
import pybind11, sys

ext_modules = [
    Extension(
        "tcp_cpp",                  
        ["tcp.cpp", "bindings_tcp.cpp"],
        include_dirs=[pybind11.get_include()],
        language="c++",
        extra_compile_args=["-std=c++17"],
    )
]

setup(
    name="tcp_cpp",
    version="0.1",
    ext_modules=ext_modules,
    python_requires=">=3.8",
)