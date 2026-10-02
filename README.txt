Магический круговой буфер (Ring buffer)
=======================================

Однофайловая реализация кругового буфера на C++ без сторонних зависимостей.
Два соседних виртуальных блока транслируются на общую физическую память, что позволяет поддерживать содержимое буфера в непрерывном и упорядоченном виде.

Требования:
    - Linux, Windows
    - Компилятор с поддержкой C++11 и выше

Состав проекта:
    tests/
        3rdparty/             Сторонние библиотеки для упрощения тестирования
        data/                 Тестовая раскадровка
        opencv_like.h         Частичный копипаст функционала из OpenCV Core чтобы не тащить всю тяжелую библиотеку
        perf_tests.cpp        Тесты производительности
        tests.cpp             Юнит-тесты
    ring_buffer.h             Реализация буфера

Компиляция на Linux
===================

Консольные [и визуальные] юнит-тесты

    clang++ tests/tests.cpp -o tests -g -O2 -Wall -Wextra -Wno-unused-function [-Dcimg_display=1 -lX11 -lpthread]

Тесты производительности

    clang++ tests/perf_tests.cpp -o perf_tests -g -O2

Компиляция на Windows
=====================

Консольные [и визуальные] юнит-тесты

    :: Поменять кодировку на UTF-8
    chcp 65001

    cl /Zi /FC tests\tests.cpp /EHsc /DEBUG /O2 [/Dcimg_display=2]

Кросс-компиляция
================

Юнит-тесты под конкретный ABI

    zig c++ -target x86_64-linux-gnu.2.16 -Wall -Werror -Wfatal-errors tests\tests.cpp -o tests_gnu.2.16

Профилирование на Linux
=======================

Проверка на утечки памяти

    valgrind --leak-check=full --show-leak-kinds=all ./tests

Профилирование по системных счетчикам

    sudo sysctl -w kernel.perf_event_paranoid=1

    perf stat --repeat=32 -dd ./perf_tests ring read 1

    argv[1] - тип теста    : malloc, mmap, ring
    argv[2] - операция     : read, write
    argv[3] - объем (у.е.) : 0, 1, 2

Поиск узких мест

    perf record -e instructions,branches,branch-misses ./perf_tests ring read 1
    perf report