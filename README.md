# 🚀 HDRVisualizer

**HDRVisualizer** is an interactive High Dynamic Range (HDR) image viewer developed in **C++**. It utilizes **OpenGL** for hardware-accelerated rendering, **OpenCV** for robust image processing, and features a modern, clean graphical interface built with **Dear ImGui**.


---

## ✨ Key Features

* **🎨 Separated Global Tonemapping:** Exposure and tone mapping remain perfectly stable. Zooming in doesn't alter the brightness or contrast of the viewed region.
* **🎯 Cursor-Centered Zoom:** Advanced UV coordinate calculation and interpolation that keeps the exact pixel you're pointing at in focus while scaling the image.
* **📦 Region of Interest (BBox) Zoom:** Right-click and drag to instantly frame and crop any specific section of the image.
* **📁 Directory Navigation:** Load entire folders and seamlessly navigate through sequences of HDR images without leaving the application.
* **🖱️ Custom Precision Cursor:** Aesthetic replacement of the default system cursor with a reactive crosshair integrated directly into the rendering viewport.

---

## 🎮 Controls & Interface Guide

| Control / Action | Input (Keyboard / Mouse) | Description |
| :--- | :--- | :--- |
| **Zoom to Cursor (In)** | Hold `Z` + Scroll Up 🖱️ | Smoothly zooms in using the current mouse position as the pivot point. |
| **Zoom to Cursor (Out)** | Hold `Z` + Scroll Down 🖱️ | Smoothly zooms out without breaking the original resolution boundaries. |
| **Region Selection (BBox)**| Right Click + Drag 🖱️ | Draws a selection box on the canvas to instantly crop and frame that specific area. |
| **Reset View** | `Reset Zoom` Button in UI | Restores the crop dimensions to the full original image size. |
| **File Navigation**| Left / Right Arrows `←` `→` | Switches to the previous or next image in the loaded directory. |
| **Parameter Adjustment** | ImGui Sliders | Real-time adjustment of exposure, gamma, saturation, and light adaptation levels. |

---

## 🎛️ Tonemapping Parameters

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

Actually, the application is working in ubuntu. I'm not sure if it works in other OS.

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