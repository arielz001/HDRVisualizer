# 🚀 HDRVisualizer

**HDRVisualizer** is an interactive High Dynamic Range (HDR) image viewer developed in **C++**. It utilizes **OpenGL** for hardware-accelerated rendering, **OpenCV** for robust image processing, and features a modern, clean graphical interface built with **Dear ImGui**.

---

## ✨ Key Features

* **🎨 Separated Global Tonemapping:** Exposure and tone mapping remain perfectly stable. Zooming or panning around doesn't alter the brightness or contrast of the viewed region.
* **🎯 Infinite Scroll Zoom:** Advanced UV coordinate tracking that keeps the exact pixel under your cursor in focus while zooming, fixed against integer truncation traps.
* **📦 Interactive BBox Selection:** Right-click and drag to create a selection box with a dedicated floating contextual menu to apply custom actions over that region.
* **🔍 Auto-Range Dynamic Colormap:** Automatically re-centers and scales the colormap radius based on the exact localized minimum and maximum HDR values inside your selection.
* **📁 Directory Navigation:** Load entire folders and seamlessly navigate through sequences of HDR images without leaving the application.
* **👁️ Real-time Pixel Matrix Overlay:** Deep zooming automatically displays a readable sub-grid showing the precise channel values (RGB) adapting text contrast dynamically to the background luminance.
* **🖱️ Custom Precision Cursor:** Aesthetic replacement of the default system cursor with a reactive crosshair integrated directly into the viewport.

---

## 🎮 Controls & Interface Guide

### 🖱️ Mouse Bindings

| Control / Action | Input (Mouse) | Description |
| :--- | :--- | :--- |
| **Zoom to Cursor** | Scroll Up / Down 🖱️ | Smoothly zooms in or out using the current mouse position as the dynamic pivot anchor. |
| **Canvas Panning** | Left Click + Drag 🖱️ | Drags and pans the viewport across the image safely clamped inside the original resolution bounds. |
| **Region Selection (BBox)** | Right Click + Drag 🖱️ | Draws a green selection box on the canvas. Releasing it unfolds a contextual action window. |
| **Pixel Highlight** | Mouse Hover | Hovering individual pixels at high zoom factors displays an isolated green border and its color properties. |

### ⌨️ Keyboard Shortcuts

| Shortcut | Action | Description |
| :--- | :--- | :--- |
| `A` | **Hard Reset Everything** ⚠️ | **Master Reset:** Restores the original full image crop, clears any active colormap range, switches back to Reinhard mode, and resets all tonemapping sliders to factory defaults. |
| `R` | **Reset Range** | Clears the localized AutoRange colormap and restores the full original dynamic range. |
| `Z` | **Reset Zoom** | Reset zoom to the original image size.
| `S` | **Cycle Color Spaces** | Instantly toggles and transitions the active color space rendering properties. |
| `Q` | **Exit App** | Instantly closes the HDR Visualizer window safely. |
| `←` / `→` | **File Navigation** | Switches to the previous or next HDR image available in the loaded directory sequence. |

---

## 🎛️ Contextual Actions & Parameters

### 🔲 BBox Floating Menu
When you perform a **Right-Click selection**, a reactive box will display two fast actions:
* **Zoom:** Crops and scales the viewport strictly to the framed region coordinates.
* **AutoRange:** Analyzes the raw HDR matrix inside the bounding box, extracts the `min` / `max` values, and normalizes the visualization scale automatically.

### 🎚️ Tonemapping Operators
The application natively implements three of the most representative tone mapping operators used in the computational photography industry:
* **Reinhard:** `Gamma`, `Intensity`, `Light Adapt`, `Color Adapt`
* **Drago:** `Gamma`, `Saturation`, `Bias`
* **Mantiuk:** `Gamma`, `Scale`, `Saturation`

---

## 🔧 Requirements & Installation

### 1. System Dependencies
Ensure you have the C++ compilers and development packages for the required graphical libraries installed (example for Ubuntu/Debian-based distributions):

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev libopenexr-dev libglfw3-dev libgl1-mesa-dev xorg-dev
```

Actually, the application is working in ubuntu 22.04 and macos m1. I'm not sure if it works in windows.

---

## To clone this project 

```bash
git clone https://github.com/arielz001/HDRVisualizer.git
cd HDRVisualizer
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make
```

---

## Related project

HDRVisualizer is inspired by [vpv](https://github.com/kidanger/vpv), a popular Image viewer for Linux and MacOS.


## To cite this software

```
@misc{hdrvisualizer2026,
  author       = {arielz001},
  title        = {HDRVisualizer: Interactive HDR Image Viewer},
  year         = {2026},
  publisher    = {GitHub},
  journal      = {GitHub repository},
  howpublished = {\url{[https://github.com/arielz001/HDRVisualizer](https://github.com/arielz001/HDRVisualizer)}}
}```