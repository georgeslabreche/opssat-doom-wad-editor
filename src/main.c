#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Generate this header file by execute generate_doom_palette.py
#include "doom_palette.h"

// Structure to hold WAD header information
typedef struct {
  char type[4];
  int32_t num_lumps;
  int32_t directory_offset;
} WadHeader;

// Structure to hold WAD directory entry information
typedef struct {
  int32_t filepos;
  int32_t size;
  char name[8];
} WadDirEntry;

// Structure to hold image Patch header
typedef struct {
  int16_t width;
  int16_t height;
  int16_t leftoffset;
  int16_t topoffset;
} DoomPatchHeader;

typedef struct {
  uint8_t topdelta; // Beginning of post; 0xFF marks the end of a column
  uint8_t length;   // Length of the post
  // Followed by 'length' pixels and a dummy byte
} DoomPost;


// Function to read the WAD header
WadHeader read_wad_header(FILE *file) {
  WadHeader header;

  // Read the type (IWAD or PWAD)
  if (fread(header.type, 1, 4, file) != 4) {
    fprintf(stderr, "Failed to read WAD type\n");
    exit(EXIT_FAILURE);
  }

  // Read the number of lumps
  if (fread(&header.num_lumps, 4, 1, file) != 1) {
    fprintf(stderr, "Failed to read number of lumps\n");
    exit(EXIT_FAILURE);
  }

  // Read the offset to the directory
  if (fread(&header.directory_offset, 4, 1, file) != 1) {
    fprintf(stderr, "Failed to read directory offset\n");
    exit(EXIT_FAILURE);
  }

  return header;
}


// Function to read a WAD directory entry
WadDirEntry read_directory_entry(FILE *file, int offset) {
  WadDirEntry entry;

  // Seek to the directory entry position
  if (fseek(file, offset, SEEK_SET) != 0) {
    fprintf(stderr, "Failed to seek to directory entry\n");
    exit(EXIT_FAILURE);
  }

  // Read the file offset of the lump
  if (fread(&entry.filepos, 4, 1, file) != 1) {
    fprintf(stderr, "Failed to read file offset\n");
    exit(EXIT_FAILURE);
  }

  // Read the size of the lump
  if (fread(&entry.size, 4, 1, file) != 1) {
    fprintf(stderr, "Failed to read lump size\n");
    exit(EXIT_FAILURE);
  }

  // Read the name of the lump
  if (fread(entry.name, 1, 8, file) != 8) {
    fprintf(stderr, "Failed to read lump name\n");
    exit(EXIT_FAILURE);
  }

  return entry;
}


// Function to find a lump in the WAD directory
WadDirEntry find_lump(FILE *file, int num_lumps, int directory_offset, const char *lump_name) {
  WadDirEntry entry;

  for (int i = 0; i < num_lumps; ++i) {
    // Read the directory entry
    entry = read_directory_entry(file, directory_offset + i * sizeof(WadDirEntry));

    // Check if the lump name matches
    if (strncmp(entry.name, lump_name, sizeof(entry.name)) == 0) {
      return entry;
    }
  }

  // Lump not found, return an entry with invalid size
  WadDirEntry invalid_entry = {0, -1, ""};
  return invalid_entry;
}


// Function to extract the patch data from the WAD file
unsigned char* extract_patch_data(FILE *file, int patch_offset, int *width, int *height) {
  fseek(file, patch_offset, SEEK_SET);

  // Read the fixed part of the Doom patch header
  DoomPatchHeader header;
  if (fread(&header, sizeof(DoomPatchHeader), 1, file) != 1) {
    fprintf(stderr, "Failed to read patch header\n");
    return NULL;
  }

  *width = header.width;
  *height = header.height;
  unsigned char *pixels = calloc(header.width * header.height, sizeof(unsigned char));
  if (!pixels) {
    fprintf(stderr, "Failed to allocate memory for pixels\n");
    return NULL;
  }

  // Allocate memory for column offsets
  int32_t *column_offsets = malloc(header.width * sizeof(int32_t));
  if (!column_offsets) {
    fprintf(stderr, "Failed to allocate memory for column offsets\n");
    free(pixels);
    return NULL;
  }
  if (fread(column_offsets, sizeof(int32_t), header.width, file) != (size_t)header.width) {
    fprintf(stderr, "Failed to read column offsets\n");
    free(column_offsets);
    free(pixels);
    return NULL;
  }

  // Extract pixel data from each column
  for (int col = 0; col < header.width; ++col) {
    fseek(file, patch_offset + column_offsets[col], SEEK_SET);
    int row = 0;
    while (row < header.height) {
      DoomPost post;
      if (fread(&post, sizeof(DoomPost), 1, file) != 1) {
        // Handle read error or end of column marker
        break;
      }

      if (post.topdelta == 0xFF) {
        break; // End of column
      }

      row += post.topdelta;

      if (row + post.length > header.height) {
        // Error handling: post extends beyond the bottom of the patch
        break;
      }

      for (int p = 0; p < post.length; ++p) {
        uint8_t pixel;
        if (fread(&pixel, 1, 1, file) != 1) {
          // Handle read error
          break;
        }
        pixels[row * header.width + col] = pixel;
        row++;
      }

      // Skip the dummy byte at the end of the post
      fseek(file, 1, SEEK_CUR);
    }
  }

  free(column_offsets);
  return pixels;
}


// Function to convert Doom palette index to RGB
void doom_index_to_rgb(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (index >= 256) {
    // Handle invalid index
    *r = *g = *b = 0; // Default to black or another default color
    return;
  }

  // Map the palette index to RGB values
  *r = DOOM_PALETTE[index][0];
  *g = DOOM_PALETTE[index][1];
  *b = DOOM_PALETTE[index][2];
}


// The main function
int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [path_to_wad_file]\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char *wad_filename = argv[1];
  FILE *file = fopen(wad_filename, "rb");
  if (!file) {
    perror("Error opening WAD file");
    return EXIT_FAILURE;
  }

  // Read WAD header
  WadHeader header = read_wad_header(file);

  // Find the SKY1 lump
  WadDirEntry sky1_entry = find_lump(file, header.num_lumps, header.directory_offset, "SKY1");
  if (sky1_entry.size == -1) {
    fprintf(stderr, "SKY1 lump not found\n");
    fclose(file);
    return EXIT_FAILURE;
  }

  // Extract the SKY1 patch data
  int width, height;
  unsigned char *pixels = extract_patch_data(file, sky1_entry.filepos, &width, &height);
  if (!pixels) {
    fclose(file);
    return EXIT_FAILURE;
  }

  printf("Extracted SKY1 patch dimensions: width = %d, height = %d\n", width, height);

  // Allocate memory for RGB data
  unsigned char *rgb_pixels = malloc(width * height * 3); // 3 bytes per pixel for RGB
  if (!rgb_pixels) {
    fprintf(stderr, "Failed to allocate memory for RGB pixels\n");
    free(pixels);
    fclose(file);
    return EXIT_FAILURE;
  }

  // Convert to RGB
  for (int i = 0; i < width * height; ++i) {
    doom_index_to_rgb(pixels[i], &rgb_pixels[i*3], &rgb_pixels[i*3+1], &rgb_pixels[i*3+2]);
  }

  // Write the pixel data to a JPEG file
  const char *output_filename = "sky1.jpeg";
  if (!stbi_write_jpg(output_filename, width, height, 3, rgb_pixels, 100)) {
    fprintf(stderr, "Failed to write JPEG image file\n");
  } else {
    printf("JPEG image saved to %s\n", output_filename);
  }

  free(rgb_pixels);
  free(pixels);
  fclose(file);
  return EXIT_SUCCESS;
}

