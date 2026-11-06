idf_component_register(
    SRCS
        "main.cc"
        "image_provider.cc"
        "detection_responder.cc"
        "snapshot.cc"
        "person_detect_model_data.cc"
    # ../../../common: dùng lại camera_pins.h của các bài Arduino
    PRIV_INCLUDE_DIRS "." "../../../common"
    PRIV_REQUIRES esp_timer esp_driver_gpio esp_driver_uart
    # 10 ảnh xám 96x96 của ví dụ gốc, dùng cho phần tự kiểm tra lúc khởi động (PD_SELF_TEST)
    EMBED_FILES
        "sample_images/image0" "sample_images/image1" "sample_images/image2"
        "sample_images/image3" "sample_images/image4" "sample_images/image5"
        "sample_images/image6" "sample_images/image7" "sample_images/image8"
        "sample_images/image9")
