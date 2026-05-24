import cv2, numpy as np


path = 'image.raw'


import os
import cv2
import numpy as np

def process_raw(file_path):
    channels = 4 
    
    try:
        # 1. Obtener el tamaño del archivo en bytes de forma automática
        file_size = os.path.getsize(file_path)
        
        # El número total de píxeles combinados de todos los canales
        total_pixels = file_size // channels 
        
        # 2. Calcular resolución automática manteniendo la proporción del sensor (1224/1024 ≈ 1.195)
        # Usamos una aproximación matemática para encontrar el ancho y alto
        aspect_ratio = 1224 / 1024  
        
        height = int(np.sqrt(total_pixels / aspect_ratio))
        width = int(height * aspect_ratio)
        
        # Ajuste por si el redondeo falla por unos pocos píxeles
        if width * height * channels != file_size:
            # Si no encaja exacto, intentamos asumir que es perfectamente cuadrada o hacemos un ajuste fino
            width = int(np.round(np.sqrt(total_pixels)))
            height = total_pixels // width

        expected_size = width * height * channels
        print(f"Resolución detectada: {width}x{height} a {channels} canales.")

        with open(file_path, "rb") as rawimg:
            data = np.fromfile(rawimg, np.uint8, expected_size)
            
        if data.size != expected_size:
            raise ValueError(f"El tamaño de los datos ({data.size}) no coincide con la resolución calculada.")
            
        image = np.reshape(data, (height, width, channels))
        
        # Procesamiento de canales (Ojo aquí, ver nota abajo)
        intensity_0 = cv2.cvtColor(image[:, :, 0], cv2.COLOR_BayerRG2BGR)
        intensity_45 = cv2.cvtColor(image[:, :, 1], cv2.COLOR_BayerRG2BGR)
        intensity_90 = cv2.cvtColor(image[:, :, 2], cv2.COLOR_BayerRG2BGR)
        intensity_135 = cv2.cvtColor(image[:, :, 3], cv2.COLOR_BayerRG2BGR)
        
        return intensity_0, intensity_45, intensity_90, intensity_135

    except FileNotFoundError:
        print(f"Error: El file no fue encontrado: {file_path}")
        return None
    except ValueError as e:
        print(f"Error de dimensiones: {e}")
        return None
    except Exception as e:
        print(f"Un error inesperado ocurrió: {e}")
        return None
intensity_0, intensity_45, intensity_90, intensity_135 = process_raw(path)

# Display the intensity_0 image
import matplotlib.pyplot as plt
plt.figure(figsize=(10, 10))
plt.imshow(intensity_0)
plt.title("Intensity_0")
plt.show()
