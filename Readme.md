# TorqueViewer

## What is it?

TorqueViewer is a simple viewer for Tribes 2 resources.

## Requirements

Currently the following is needed to build:

- SDL3
- wgpu-native

## Building

	mkdir build
	cd build
	cmake -DUSE_WGPU_NATIVE=1 -DWGPU_NATIVE_PATH=path/to/wgpu-native ..
	make

## Usage

Assuming you have compiled the executable, you need to supply a list of paths or volumes in which the desired asset and associated asset files are located. The final parameter should then be the resource file you want to view to start off with. e.g.

	./TribesViewer . base.zip models/harmor.dts

The project is currently WIP so don't expect anything to render yet.
