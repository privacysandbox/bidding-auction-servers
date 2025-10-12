#!/usr/bin/env python3
"""
Setup script for secure_invoke Python package.

This package provides Python bindings for the SecureInvoke cryptographic library
used in Privacy Sandbox bidding and auction systems.
"""

from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext
import os
import subprocess
import sys
from pathlib import Path

class BazelBuildExt(build_ext):
    """Custom build extension that uses Bazel to build the C++ library."""
    
    def run(self):
        """Build the C++ library using Bazel before building Python extensions."""
        
        # Get the project root (assuming we're in tools/secure_invoke/python)
        project_root = Path(__file__).parent.parent.parent.parent.absolute()
        os.chdir(project_root)
        
        print(f"Building C++ library from: {project_root}")
        
        # Build the shared library using Bazel
        try:
            subprocess.run([
                "./builders/tools/bazel-debian", "build", 
                "//tools/secure_invoke:libsecure_invoke.so"
            ], check=True)
            
            # Copy the built library to the package lib directory
            src_lib = project_root / "bazel-bin/tools/secure_invoke/libsecure_invoke.so"
            dst_lib = Path(__file__).parent / "secure_invoke_crypto/lib/libsecure_invoke.so"
            
            # Ensure lib directory exists
            dst_lib.parent.mkdir(exist_ok=True)
            
            if src_lib.exists():
                import shutil
                shutil.copy2(src_lib, dst_lib)
                print(f"Copied library: {src_lib} -> {dst_lib}")
                
                # Also copy dependent libraries if they exist
                cddl_src = project_root / "tools/secure_invoke/libcddl.so"
                if cddl_src.exists():
                    cddl_dst = dst_lib.parent / "libcddl.so"
                    shutil.copy2(cddl_src, cddl_dst)
                    print(f"Copied dependent library: {cddl_src} -> {cddl_dst}")
                else:
                    print("Warning: libcddl.so not found, package may need manual library setup")
            else:
                raise FileNotFoundError(f"Built library not found: {src_lib}")
                
        except subprocess.CalledProcessError as e:
            print(f"Failed to build C++ library: {e}")
            sys.exit(1)
        except Exception as e:
            print(f"Error during build: {e}")
            sys.exit(1)
        
        # Continue with normal extension building (if any)
        super().run()


# Read version from version file
def get_version():
    version_file = Path(__file__).parent / "secure_invoke_crypto" / "_version.py"
    if version_file.exists():
        with open(version_file) as f:
            exec(f.read())
            return locals()['__version__']
    return "0.1.0"

# Read long description from README
def get_long_description():
    readme_file = Path(__file__).parent / "README.md"
    if readme_file.exists():
        with open(readme_file, encoding='utf-8') as f:
            return f.read()
    return ""

setup(
    name="secure-invoke-crypto",
    version=get_version(),
    author="Privacy Sandbox Team",
    author_email="privacy-sandbox@example.com",
    description="Python cryptographic bindings for SecureInvoke library",
    long_description=get_long_description(),
    long_description_content_type="text/markdown",
    url="https://github.com/privacysandbox/bidding-auction-servers",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Security :: Cryptography",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    python_requires=">=3.8",
    install_requires=[
        "requests>=2.25.0",
        "typing-extensions>=3.7.4; python_version<'3.8'",
    ],
    extras_require={
        "dev": [
            "pytest>=6.0",
            "pytest-cov>=2.0",
            "black>=21.0",
            "flake8>=3.8",
            "mypy>=0.900",
        ],
        "async": [
            "aiohttp>=3.7.0",
        ],
    },
    package_data={
        "secure_invoke_crypto": [
            "lib/*.so",
            "src/*.cc",
            "src/*.h",
        ],
    },
    include_package_data=True,
    cmdclass={
        'build_ext': BazelBuildExt,
    },
    entry_points={
        "console_scripts": [
            "secure-invoke-test=secure_invoke_crypto.tests.test_encrypt_http:main",
            "secure-invoke-demo=secure_invoke_crypto.crypto:demo",
        ],
    },
    zip_safe=False,  # C extensions can't be loaded from zip files
)
