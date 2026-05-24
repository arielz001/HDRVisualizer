# 🚀 HPVis (High-Polarized & HDR Visualizer)

**HPVis** is an interactive High Dynamic Range (HDR) and Polarized image viewer developed in **C++**. It utilizes **OpenGL** for hardware-accelerated rendering, **OpenCV** for robust image processing, and features a modern, clean graphical interface built with **Dear ImGui**. 

The engine natively decodes raw polarization data alongside standard HDR formats, providing an optimized multi-panel monolithic grid layout for advanced computer vision analysis.

---

## ✨ Key Features

* **🎨 Separated Global Tonemapping:** Exposure and tone mapping remain perfectly stable. Zooming or panning around doesn't alter the brightness or contrast of the viewed region. The original raw data remains unaltered during inspection.
* **📸 Native Polarization Decoding:** Automatically detects and unpacks raw polarization mosaic formats (`.raw`)
* **🧩 Monolithic Multi-Panel Grid:** Splits and computes polarized mosaics into 4 standard polarization channels ($0^\circ, 45^\circ, 90^\circ, 135^\circ$), rendering a synchronized 6-panel workspace that includes real-time calculation of:
  * **DoLP** (Degree of Linear Polarization) mapped via `COLORMAP_JET`.
  * **AoLP** (Angle of Linear Polarization) mapped via `COLORMAP_HSV`.
* **🎯 Scroll Zoom:** Advanced UV coordinate tracking that keeps the exact pixel under your cursor in focus while zooming, fixed against integer truncation traps. Works seamlessly across all sub-panels in unison.
* **📦 Interactive BBox Selection:** Right-click and drag to create a selection box with a dedicated floating contextual menu to apply custom actions over that region.
* **🔍 Auto-Range Dynamic Colormap:** Automatically re-centers and scales the colormap radius based on the exact localized minimum and maximum HDR values inside your selection.
* **📁 Directory Navigation:** Load entire folders and seamlessly navigate through sequences of HDR images without leaving the application.
* **👁️ Real-time Pixel Matrix Overlay:** Deep zooming automatically displays a readable sub-grid showing the **precise, unaltered raw float values (up to 4 decimal places)** instead of post-processed LDR values. Text contrast adapts dynamically to the background luminance.
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
| `A` | **Hard Reset Everything** ⚠️ | **Master Reset:** Restores the original full image crop, clears any active colormap range, switches back to Reinhard mode, resets polarized buffers, and sets all tonemapping sliders to factory defaults. |
| `R` | **Reset Range** | Clears the localized AutoRange colormap and restores the full original dynamic range. |
| `Z` | **Reset Zoom** | Resets zoom to the original image boundaries. |
| `S` | **Cycle Color Spaces** | Instantly toggles and transitions the active color space rendering properties. |
| `Q` | **Exit App** | Instantly closes the visualizer window safely. |
| `←` / `→` | **File Navigation** | Switches to the previous or next image available in the loaded directory sequence. |

---

## 🎛️ Contextual Actions & Parameters

### 🔲 BBox Floating Menu
When you perform a **Right-Click selection**, a reactive box will display two fast actions:
* **Zoom:** Crops and scales the viewport strictly to the framed region coordinates.
* **AutoRange:** Analyzes the raw HDR matrix inside the bounding box, extracts the `min` / `max` values, and normalizes the visualization scale automatically.

### 🎚️ Tonemapping Operators
The application natively implements three of the most representative tone mapping operators used in the computational photography industry. Changing operators **only affects visualization**, leaving HUD pixel readings intact:
* **Reinhard:** `Gamma`, `Intensity`, `Light Adapt`, `Color Adapt`
* **Drago:** `Gamma`, `Saturation`, `Bias`
* **Mantiuk:** `Gamma`, `Scale`, `Saturation`

---

## 🔧 Requirements & Installation

### 1. System Dependencies
The application is fully compatible and tested under **Ubuntu 22.04 LTS** and **macOS (M1/Silicon architecture)** natively. *(Windows compatibility is not officially tested)*.

Ensure you have the C++ compilers and development packages for the required graphical libraries installed:

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev libopenexr-dev libglfw3-dev libgl1-mesa-dev xorg-dev
```

In MacOS, you may need install the following dependencies:

```bash
brew install cmake opencv openexr glfw
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