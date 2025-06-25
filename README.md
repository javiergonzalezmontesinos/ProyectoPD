Sistema de Control de Acceso con ESP32

Descripción
Este proyecto implementa un sistema de control de acceso basado en el microcontrolador ESP32, diseñado para gestionar el acceso a una puerta mediante múltiples métodos de autenticación: RFID, PIN, Telegram, y un temporizador web. El sistema incluye un servidor web para configuración y monitoreo, registro de eventos en una tarjeta SD, notificaciones en tiempo real vía Telegram, y un LED RGB (WS2812) para indicar el estado del sistema. El cierre eléctrico, alimentado por un transformador de 220V AC a 12V AC, es controlado mediante un relé de 5V.
El proyecto es ideal para aplicaciones de seguridad en hogares, oficinas o espacios controlados, ofreciendo una solución modular, escalable y de bajo costo.
Características

Autenticación Múltiple:
Tarjetas RFID (RC522) con verificación de UID.
PIN de 4 dígitos ingresado vía servidor web.
Comandos remotos vía Telegram (/abrir, nombre, PIN).
Temporizador web configurable (1–3600 segundos).


Interfaz Web:
Servidor web responsivo para monitoreo (estado de la puerta, historial) y gestión de usuarios (protegida por contraseña).
Título consistente: "Panel de Control".
Soporte para caracteres acentuados (UTF-8).


Registro de Eventos:
Almacena hasta 15 eventos en /access_log.txt en la tarjeta SD (formato: Fecha,Método,ID,Usuario,Estado).
Sincronización de hora vía servidor NTP.


Indicadores Visuales:
LED RGB WS2812:
Rojo sólido: Acceso restringido.
Verde parpadeante (300ms): Acceso concedido, puerta cerrada.
Amarillo sólido: Acceso concedido, puerta abierta.
Rojo parpadeante (200ms): Intrusión detectada.


LED integrado (GPIO 2): Indica conexión WiFi durante el arranque.


Notificaciones:
Alertas en tiempo real vía Telegram para accesos, intrusiones, y cambios de usuarios.
Comando /ip para obtener la IP del ESP32.


Control de Puerta:
Relé de 5V controla un cierre eléctrico de 12V AC, alimentado por un transformador (220V AC → 12V AC).
Sensor magnético detecta el estado de la puerta (abierta/cerrada).



Requisitos:
Hardware:

Componente              Descripción



ESP32                   Microcontrolador (e.g., DevKit V1)


Lector RFID RC522       Autenticación por tarjetas RFID


Módulo Tarjeta SD       Almacenamiento de logs y usuarios


Sensor Magnético        Detecta estado de la puerta


Relé de 5V              Controla el cierre eléctrico


Cierre Eléctrico        12V AC (e.g., pestillo o electroimán)


Transformador AC        220V AC → 12V AC, 12–20VA


LED RGB WS2812          Indicador visual de estados


Cables y Protoboard     Para conexiones


Software:

Entorno: Arduino IDE o PlatformIO.
Librerías:
WiFi, AsyncTCP, ESPAsyncWebServer
Adafruit_NeoPixel
UniversalTelegramBot, ArduinoJson
MFRC522
SD, SPI
time


Dependencias: Instalar las librerías desde el Administrador de Librerías de Arduino.
Configuración:
Credenciales WiFi y token de Telegram Bot (definir en el código).
Tarjeta SD formateada en FAT32.



Configurar el Hardware:

Conectar los componentes según el Esquema de Cableado.
Verificar las conexiones del transformador y el cierre eléctrico según Cableado del Cierre Eléctrico.


Configurar el Software:

Actualizar las siguientes constantes en el código:const char* ssid = "TU_RED_WIFI";
const char* password = "TU_CONTRASEÑA_WIFI";
const char* botToken = "TU_TOKEN_TELEGRAM";
const String chatId = "TU_CHAT_ID";


Instalar las librerías requeridas desde el Administrador de Librerías.
Seleccionar la placa "ESP32 Dev Module" y el puerto COM correspondiente.


Cargar el Código:

Compilar y cargar el código al ESP32.
Abrir el Monitor Serial (115200 baudios) para depuración.


Inicializar la Tarjeta SD:

Insertar una tarjeta SD formateada en FAT32.
Crear los archivos /users.txt y /access_log.txt si no se generan automáticamente.


Probar el Sistema:

Conectar el ESP32 a la red WiFi (el LED integrado parpadea durante la conexión).
Escanear una tarjeta RFID, ingresar un PIN vía web, o usar Telegram (/abrir).
Verificar que el relé activa el cierre eléctrico y el LED RGB muestra el estado correcto.
Acceder al servidor web desde un navegador (usar la IP mostrada en Telegram o el Monitor Serial).



Estructura del Repositorio
├── src/
│   └── main.cpp                # Código principal del proyecto
├── images/
│   ├── Añadir_usuario.png
│   ├── Diagrama_bloques.png
│   ├── Lista_usuarios.png
│   └── Pantalla_principal.png  
├── docs/
│   ├── Informe_ProyectoPD.pdf  # Informe detallado del proyecto
│   └── Informe_ProyectoPD.md   # Informe detallado del proyecto en formato MarkDown
├── README.md                   # Este archivo
└── LICENSE                     # Licencia MIT

Uso

Autenticación:

RFID: Escanear una tarjeta registrada en /users.txt.
PIN: Ingresar un PIN de 4 dígitos en la interfaz web.
Telegram: Enviar /abrir <nombre> <PIN> al bot configurado.
Temporizador: Configurar un tiempo (1–3600s) en la web para activar el cierre.


Monitoreo:

Acceder al servidor web desde un navegador en la misma red (IP del ESP32).
Visualizar el estado de la puerta, historial de accesos (hasta 15 eventos), y gestionar usuarios.
Recibir notificaciones Telegram para accesos, intrusiones, o cambios de configuración.


Indicadores:

Observar el LED RGB para el estado del sistema (rojo, verde, amarillo, rojo parpadeante).
Verificar el LED integrado para conexión WiFi.



Contribuciones
¡Las contribuciones son bienvenidas! Por favor, sigue estos pasos:

Crea un fork del repositorio.
Crea una nueva rama (git checkout -b feature/nueva-funcion).
Realiza tus cambios y haz commit (git commit -m "Añadir nueva función").
Sube los cambios (git push origin feature/nueva-funcion).
Abre un Pull Request describiendo los cambios.

Problemas Conocidos

RFID RC522: Algunos módulos pueden fallar si no están bien soldados o alimentados (usar 3.3V).
Tarjeta SD: Asegurarse de que esté formateada en FAT32 y que los archivos sean accesibles.
Transformador AC: Verificar que proporcione suficiente corriente (12–20VA) para el cierre eléctrico.

Reporta cualquier problema en la sección de Issues.
Licencia
Este proyecto está bajo la Licencia MIT. Puedes usar, modificar y distribuir el código libremente, siempre que incluyas la licencia original.
Documentación Adicional

Informe del Proyecto: Detalles técnicos, desafíos, y lecciones aprendidas.
Esquema de Cableado: Conexiones de todos los componentes.
Cableado del Cierre Eléctrico: Instrucciones para el transformador y cierre de 12V AC.
Diagrama de Bloques: Arquitectura del sistema.

Agradecimientos

A mi profesor por la orientación proporcionada.
A las comunidades de Arduino y ESP32 por los recursos y ejemplos.
A los desarrolladores de las librerías utilizadas.

Autor: [Tu Nombre]Fecha: 25 de junio de 2025Contacto: [tu-email@ejemplo.com]Repositorio: https://github.com/[tu-usuario]/sistema-control-acceso-esp32