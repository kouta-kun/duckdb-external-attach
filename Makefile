.PHONY: all clean format debug release pull update

all: release

OSX_BUILD_UNIVERSAL_FLAG=
ifeq (${OSX_BUILD_UNIVERSAL}, 1)
	OSX_BUILD_UNIVERSAL_FLAG=-DOSX_BUILD_UNIVERSAL=1
endif
BUILD_FLAGS=${OSX_BUILD_UNIVERSAL_FLAG} -DBUILD_SHELL=0 -DBUILD_UNITTESTS=0 -DBUILD_TESTING=0


pull:
	git submodule init
	git submodule update

clean:
	rm -rf build

debug: pull
	mkdir -p build/debug && \
	cd build/debug && \
	cmake -DCMAKE_BUILD_TYPE=Debug ${BUILD_FLAGS} ../../duckdb/CMakeLists.txt -DEXTENSION_STATIC_BUILD=1 -DEXTERNAL_EXTENSION_DIRECTORIES=../../duckdb-external-attach -B. && \
	make attach_extension_loadable_extension

ext-only: pull
	mkdir -p build/release && \
	cd build/release && \
	cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEXTENSION_STATIC_BUILD=0 ${BUILD_FLAGS} ../../duckdb/CMakeLists.txt -DEXTERNAL_EXTENSION_DIRECTORIES=../../duckdb-external-attach -B. && \
	make attach_extension_loadable_extension

release: pull 
	mkdir -p build/release && \
	cd build/release && \
	cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ${BUILD_FLAGS} ../../duckdb/CMakeLists.txt -DEXTENSION_STATIC_BUILD=1 -DEXTERNAL_EXTENSION_DIRECTORIES=../../duckdb-external-attach -B. && \
	make attach_extension_loadable_extension

fulltree: pull release
	cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEXTENSION_STATIC_BUILD=1 ${BUILD_FLAGS} ../../duckdb/CMakeLists.txt -DEXTERNAL_EXTENSION_DIRECTORIES=../../duckdb-external-attach -B. && \
	cd build/release && \
	make


test: fulltree
	./build/release/test/unittest --test-dir . "[hexhamm]"

perf-test: ext-only
	bash run-test.bash

format:
	cp duckdb/.clang-format .
	clang-format --sort-includes=0 -style=file -i attach_duckdb.cpp
	cmake-format -i CMakeLists.txt
	rm .clang-format

update:
	git submodule update --remote --merge

