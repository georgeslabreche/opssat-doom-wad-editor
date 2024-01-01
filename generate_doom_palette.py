#!/usr/bin/env python3

def generate_palette_header(input_file, output_file):
  with open(input_file, 'r') as file:
    colors = file.readlines()

  with open(output_file, 'w') as file:
    file.write("// THIS IS GENERATED CODE\n\n")
    file.write("#include <stdint.h>\n\n")
    file.write("static const uint8_t DOOM_PALETTE[256][3] = {\n")
    for color in colors:
      r, g, b = color.strip().split(", ")
      file.write(f"  {{{r}, {g}, {b}}},\n")
    file.write("};\n")

generate_palette_header('doom_palette.csv', 'src/doom_palette.h')
