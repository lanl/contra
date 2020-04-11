Contra
======



Installation
----------------

Contra currently depends on [Legion](https://legion.stanford.edu/) and 
[LLVM](http://llvm.org/) version 9.  If you build Legion yourself, it *MUST* be
build as a shared library (i.e. with -DBUILD_SHARED_LIBS=on).

Building LLVM requires considerable resources.  The simplest approach is to install
pre-build binaries via your Linux package manager.  For example, on Ubuntu

    sudo apt install llvm-9-dev clang-9 libclang-9-dev

If prebuilt packages are not available, you can build them with CMake.

    # Build third-party libraries (optional)
    cd external
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=$PWD/install ..
    make -j
    cd ../../

If you use pre-built binarys for LLVM, make sure to disable it when building the other
dependencies with `-DBUILD_LLVM=off`.  Building Contra is simple,
        
    # Build contra
    mkdir build
    cd build
    cmake -DCMAKE_PREFIX_PATH=$PWD/../external/build/install -DCMAKE_INSTALL_PREFIX=$PWD/install ..
        

Getting Started
---------------

To run one of the examples,

    ./contra ../examples/contra/00_hello_world/hello.cta

will produce

    Hello World!
    
There are a bunch of tests and examples that you can try.  They are located in
the `examples` and `testing` folders.

Documentation
---------------

See the [wiki](docs/home.md) for more
information on the Contra language and the compiler/interpreter. 
