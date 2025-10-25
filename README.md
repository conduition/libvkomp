# libvkomp

Accelerate your code with slim and fast Vulkan compute shaders.

## Building

Install dependencies:

```sh
sudo apt install build-essential libvulkan-dev
```

Build the static library:

```sh
make libvkomp.a
```

## Testing

You'll need a GLSL compiler to build shaders. I suggest `glslangValidator` from the `glslang-tools` deb package.

```sh
sudo apt install glslang-tools
```

Now build the tests and run them:

```sh
make test
```
