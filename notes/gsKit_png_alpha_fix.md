# Temporary fix for the PNG alpha bug in gsKit

In the current implementation of `gsKit_texture_png` (inside the `PNG_COLOR_TYPE_RGB_ALPHA` branch), there is a bug in the alpha conversion logic that causes fully transparent pixels to become fully opaque. In practice, this often appears as black pixels or a black background in areas that are supposed to remain transparent.

## Problem

The problematic line looks like this:

```c
Pixels[k++].a = 128 - ((int)row_pointers[i][4*j+3] * 128 / 255);
```

This formula inverts the alpha value:

- `alpha = 0` in PNG (fully transparent pixel) becomes `128`, which is maximum opacity for the GS.
- `alpha = 255` in PNG (fully opaque pixel) becomes `0`.

As a result, transparent parts of the image are rendered as visible.

## Fix

Replace the buggy line with the following one:

```c
Pixels[k++].a = ((int)row_pointers[i][4*j+3] * 128 + 127) / 255;
```

This version correctly scales the PNG alpha channel from the `0..255` range into the GS `0..128` range without inverting it:

- `0` stays `0` — the pixel remains fully transparent.
- `255` becomes `128` — the pixel becomes fully opaque.
- Intermediate values are scaled proportionally.

## What to change

Open the file where `gsKit_texture_png` is implemented (usually `gsKit_texture_png.c` or a similarly named source file), then find this branch:

```c
if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB_ALPHA)
```

Inside the pixel processing loop, replace the code with this:

```c
for (i=0;i<height;i++) {
    for (j=0;j<width;j++) {
        Pixels[k].r = row_pointers[i][4*j];
        Pixels[k].g = row_pointers[i][4*j+1];
        Pixels[k].b = row_pointers[i][4*j+2];
        Pixels[k++].a = ((int)row_pointers[i][4*j+3] * 128 + 127) / 255;

        /* Old incorrect version:
         * Pixels[k++].a = 128 - ((int)row_pointers[i][4*j+3] * 128 / 255);
         */
    }
}
```

## Result

After this fix:

- fully transparent pixels will no longer appear as black;
- semi-transparent areas will render correctly;
- PNG images with alpha will display as expected.

## Quick test

To verify the fix, load a PNG that contains:

- a fully transparent background;
- semi-transparent elements;
- fully opaque elements.

Before the fix, the transparent background may appear black. After the fix, it should remain truly transparent.

## Note

This is a temporary local fix until the correction is merged into the main `gsKit` branch.

---

# Временное исправление бага альфа-канала PNG в gsKit

В текущей версии `gsKit_texture_png` (в ветке обработки `PNG_COLOR_TYPE_RGB_ALPHA`) есть ошибка в конвертации альфа-канала, из-за которой полностью прозрачные пиксели становятся полностью непрозрачными. Визуально это часто выглядит как чёрные пиксели или чёрный фон в тех местах, которые должны оставаться прозрачными.

## Проблема

Проблемная строка выглядит так:

```c
Pixels[k++].a = 128 - ((int)row_pointers[i][4*j+3] * 128 / 255);
```

Эта формула инвертирует альфу:

- `alpha = 0` в PNG (полностью прозрачный пиксель) превращается в `128`, то есть в максимально непрозрачный пиксель для GS.
- `alpha = 255` в PNG (полностью непрозрачный пиксель) превращается в `0`.

В результате прозрачные области изображения начинают рисоваться как видимые.

## Исправление

Нужно заменить проблемную строку на следующую:

```c
Pixels[k++].a = ((int)row_pointers[i][4*j+3] * 128 + 127) / 255;
```

Эта версия корректно масштабирует альфа-канал PNG из диапазона `0..255` в диапазон GS `0..128` без инверсии:

- `0` остаётся `0` — пиксель остаётся полностью прозрачным.
- `255` становится `128` — пиксель становится максимально непрозрачным.
- Промежуточные значения преобразуются пропорционально.

## Что нужно поменять

Откройте файл, в котором реализована функция `gsKit_texture_png` (обычно это `gsKit_texture_png.c` или аналогичный файл), и найдите ветку:

```c
if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB_ALPHA)
```

Внутри цикла обработки пикселей замените код на такой:

```c
for (i=0;i<height;i++) {
    for (j=0;j<width;j++) {
        Pixels[k].r = row_pointers[i][4*j];
        Pixels[k].g = row_pointers[i][4*j+1];
        Pixels[k].b = row_pointers[i][4*j+2];
        Pixels[k++].a = ((int)row_pointers[i][4*j+3] * 128 + 127) / 255;

        /* Старая неправильная версия:
         * Pixels[k++].a = 128 - ((int)row_pointers[i][4*j+3] * 128 / 255);
         */
    }
}
```

## Результат

После этой правки:

- полностью прозрачные пиксели больше не будут отображаться как чёрные;
- полупрозрачные области будут отображаться корректно;
- PNG с альфа-каналом начнут рендериться так, как ожидается.

## Быстрая проверка

Для проверки достаточно загрузить PNG, в котором есть:

- полностью прозрачный фон;
- полупрозрачные элементы;
- полностью непрозрачные элементы.

До исправления прозрачный фон может отображаться как чёрный. После исправления он должен стать действительно прозрачным.

## Примечание

Это временный локальный фикс до тех пор, пока исправление не будет внесено в основную ветку `gsKit`.