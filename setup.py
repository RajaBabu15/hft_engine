from pybind11.setup_helpers import Pybind11Extension, build_ext
from pybind11 import get_cmake_dir
from setuptools import setup, Extension, find_packages
import glob
import os
import platform

# Read version from __init__.py or set it here
__version__ = "2.0.0"

# Determine compiler-specific flags
compiler_flags = []
if platform.system() == "Darwin":  # macOS
    compiler_flags.extend(["-mcpu=native", "-O3", "-DNDEBUG"])
elif platform.system() == "Linux":
    compiler_flags.extend(["-march=native", "-mtune=native", "-O3", "-DNDEBUG"])

# Define the extension module
ext_modules = [
    Pybind11Extension(
        "hft_engine_cpp",
        sorted(glob.glob("src/python_bindings.cpp") + 
               [f for f in glob.glob("src/core/*.cpp") if not f.endswith('redis_client.cpp')] + 
               glob.glob("src/order/*.cpp")),
        include_dirs=[
            "include",
            # Path to pybind11 headers
        ],
        cxx_std=17,
        define_macros=[("VERSION_INFO", f'"{__version__}"')],
        extra_compile_args=compiler_flags,
    ),
]

# Read long description from README
try:
    with open("README.md", "r", encoding="utf-8") as fh:
        long_description = fh.read()
except FileNotFoundError:
    long_description = "High-performance HFT Trading Engine with Python bindings"

setup(
    name="hft-engine-cpp",
    version=__version__,
    author="HFT Engine Team",
    author_email="dev@hftengine.com",
    description="High-performance, low-latency trading engine with Python bindings",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/RajaBabu15/hft_engine",
    project_urls={
        "Bug Tracker": "https://github.com/RajaBabu15/hft_engine/issues",
        "Documentation": "https://hft-engine.readthedocs.io/",
        "Source Code": "https://github.com/RajaBabu15/hft_engine",
    },
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Intended Audience :: Financial and Insurance Industry",
        "License :: OSI Approved :: MIT License",
        "Operating System :: Unix",
        "Operating System :: POSIX",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: MacOS",
        "Programming Language :: C++",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Topic :: Office/Business :: Financial :: Investment",
        "Topic :: Scientific/Engineering",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    keywords="hft trading finance low-latency cpp python bindings",
    python_requires=">=3.8",
    install_requires=[
        "pybind11>=2.6.0",
    ],
    extras_require={
        "dev": [
            "pytest>=6.0",
            "pytest-benchmark",
            "black",
            "isort",
            "mypy",
        ],
        "docs": [
            "sphinx>=4.0",
            "sphinx-rtd-theme",
        ],
    },
    zip_safe=False,
    include_package_data=True,
)
