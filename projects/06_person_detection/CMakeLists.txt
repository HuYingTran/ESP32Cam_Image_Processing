# Bài #6 - Phát hiện có người (ESP-IDF + esp-tflite-micro)
# Build trong thư mục này (build dir phải ngắn, xem README):  idf.py -B C:\esp-build\06 build flash monitor
# Bản không ESP-NN (kernel C tham chiếu) để so sánh:  xem README, mục "Đo ESP-NN"
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(06_person_detection)
