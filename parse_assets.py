from skimage import io
import numpy as np
from sklearn.cluster import KMeans


def compress_palette(img):
    imshape = img.shape
    rows, cols, _ = imshape

    # get only nonzero pixels
    nonzero_inds = img[:, :, 3] > 0
    nonzero_pixels = img[nonzero_inds][:, :3]

    # get 3 means, representing 3 primary colors in palette
    clt = KMeans(n_clusters=3)
    clt.fit(nonzero_pixels)
    palette = clt.cluster_centers_.astype(np.uint8)
    print(palette)

    # calculate closest color to each pixel
    img_compressed_palette = np.zeros(imshape, dtype=np.uint8)
    for i in range(rows):
        for j in range(cols):
            # get pixel
            pixel = img[i, j]
            # if pixel is transparent, skip
            if pixel[3] == 0:
                continue
            # get closest color
            closest_color = palette[np.argmin(np.linalg.norm(clt.cluster_centers_ - pixel[:3], axis=1))]
            # set pixel to closest color
            img_compressed_palette[i, j] = np.concatenate([closest_color, [255]])
    
    return palette, img_compressed_palette

def preprocess(fname):
    print('Preprocessing: {}'.format(fname))
    # read image
    img = io.imread(fname)
    # set opacity to full or 0
    img[:, :, 3] = (img[:, :, 3] > 255 / 2) * 255
    # get shape
    imshape = img.shape
    # get number of rows and columns
    rows = imshape[0]
    cols = imshape[1]
    # check that it can be parsed into multiple tiles
    assert rows % 8 == 0
    assert cols % 8 == 0

    return compress_palette(img)

def rescale(img, scale):
    rows, cols, _ = img.shape
    img_downscaled = img[::scale, ::scale, :]
    img_upscaled = np.zeros((rows, cols, 4), dtype=np.uint8)
    for i in range(rows // scale):
        for j in range(cols // scale):
            img_upscaled[i * scale:(i + 1) * scale, j * scale:(j + 1) * scale, :] = img_downscaled[i, j]
    return img_upscaled

def tile_count (img):
    rows, cols, _ = img.shape
    tiles = []
    for i in range(rows // 8):
        for j in range(cols // 8):
            tile = img[i * 8:(i + 1) * 8, j * 8:(j + 1) * 8, :]
            # check if tile is already in list
            if tile.tolist() not in tiles:
                tiles.append(tile.tolist())
    
    return len(tiles), tiles

def parse_background(fname, max_tiles=160, struct_name='BackgroundData'):
    # preprocess and compress palette
    palette, img_compressed_palette = preprocess(fname)

    # try rescaling image if tile count is too high
    scale = 1
    img_rescaled = img_compressed_palette
    tilenum, tiles = tile_count(img_rescaled)
    # only letting background use 200 tiles
    while tilenum > max_tiles:
        scale *= 2
        img_rescaled = rescale(img_compressed_palette, scale)
        tilenum, tiles = tile_count(img_rescaled)

    io.imshow(img_rescaled)
    io.show()

    # get number of tiles
    print('Number of tiles: {}'.format(tilenum))
    # get scale
    print('Scale: {}'.format(scale))

    # generate correspondence between pixel region and tile
    rows, cols = img_rescaled.shape[:2]
    print("rows: {}, cols: {}".format(rows, cols))

    # generate background tile index map
    backgroundTileNum = np.zeros(64 * 60, dtype=np.uint8)
    for i in range(rows // 8):
        for j in range(cols // 8):
            tile = img_rescaled[i * 8:(i + 1) * 8, j * 8:(j + 1) * 8, :]
            ind = tiles.index(tile.tolist())
            backgroundTileNum[(rows // 8 - i - 1) * 64 + cols // 8 - j - 1] = ind

    # write to struct
    bg_cpp = ''
    bg_cpp += ("struct " + struct_name + " {\n")
    
    # background palette
    bg_cpp += ("  PPU466::Palette color = {\n")
    for color in palette:
        line = ''
        for value in color:
            line += str(value) + ','
        bg_cpp += ('    glm::u8vec4(' + line + '255),\n')
    bg_cpp += ('    glm::u8vec4(0,0,0,0),\n')
    bg_cpp += ('  };\n')

    # tile list
    bg_cpp += ("  uint32_t tileCount = " + str(tilenum) + ";\n")
    bg_cpp += ("  uint8_t tile_inds[" + str(tilenum) + "][64] = {\n")
    for tile in tiles:
        line = ''
        for row in tile:
            for pixel in row:
                # convert to palette index
                if pixel[3] == 0:
                    line += "3,"
                else:
                    ind = np.argmin(np.linalg.norm(palette - pixel[:3], axis=1))
                    line += str(ind) + ','
        bg_cpp += ('    {' + line.strip(',') + '},\n')
    bg_cpp += ("  };\n")

    # background tile index map
    bg_cpp += ("  uint8_t backgroundTileNum[64 * 60] = {\n")
    line = ''
    count = 0
    for num in backgroundTileNum:
        line += str(num) + ','
        count += 1
        if count % 64 == 0:
            line += "\n    "
    bg_cpp += ('    ' + line.strip(',').strip() + '\n  };\n')
    bg_cpp += ("};\n")

    return bg_cpp

def parse_sprite(fname):
    # preprocess and compress palette
    palette, img_compressed_palette = preprocess(fname)

    # separate into 8x8 tiles
    rows, cols, _ = img_compressed_palette.shape
    tiles = []
    for i in range(rows // 8):
        for j in range(cols // 8):
            tile = img_compressed_palette[i * 8:(i + 1) * 8, j * 8:(j + 1) * 8, :]
            tiles.append(tile)
    for tile in tiles:
        io.imshow(tile)
        io.show()

    sprite_cpp = ''

    # write struct header

    sprite_cpp += ('struct LanderData {\n')
    # write color
    sprite_cpp += ('  PPU466::Palette color = {\n')
    for color in palette:
        line = ''
        for value in color:
            line += str(value) + ','
        sprite_cpp += ('    glm::u8vec4(' + line  + '255),\n')
    sprite_cpp += ('    glm::u8vec4(0,0,0,0),\n')
    sprite_cpp += ('  };\n')
    # write tile data
    sprite_cpp +=  ('  uint8_t tile_inds[4][64] = {\n')
    for tile in tiles:
        line = ''
        for i in range(8):
            for j in range(8):
                pixel = tile[i, j]
                # if pixel is transparent, skip
                if pixel[3] == 0:
                    line += "3,"
                else:
                    # get index in palette
                    index = np.argmin(np.linalg.norm(palette - pixel[:3], axis=1))
                    line += str(index) + ','
        sprite_cpp += ('    {' + line.strip(',') + '},\n')  
    sprite_cpp += ('  };\n') 


    sprite_cpp += ('};\n\n')
    return sprite_cpp
    

def read_sprite(fname):
    infile = open(fname, 'r')
    palette = []
    for i in range(3):
        palette.append([int(x) for x in infile.readline().split(',')])
    palette.append([0,0,0])
    palette = np.array(palette)
    print(palette)


    tiles = []
    tilenum = int(infile.readline())
    for i in range(tilenum):
        tile = [int(x) for x in infile.readline().split(',')]
        print(tile)
        tiles.append(np.array(tile).reshape(8,8))
    print(tiles)

    img = np.zeros((4, 8, 8, 3), dtype=np.uint8)
    for i in range(4):
        img[i] = palette[tiles[i]]
        io.imshow(img[i])
        io.show()




if __name__ == '__main__':
    # # combine both images
    # rows, cols, _ = img.shape
    # img_combined = np.zeros((rows, cols, 4), dtype=np.uint8)
    # for i in range(rows):
    #     for j in range(cols):
    #         if img[i, j, 3] == 0:
    #             if star[i, j, 3] == 0:
    #                 img_combined[i, j] = [0,0,0,255]
    #             else:
    #                 img_combined[i, j] = star[i, j]
    #         else:
    #             img_combined[i, j] = img[i, j]

    # io.imshow(img_combined)
    # io.show()

    # generate c++ file
    lander_struct = parse_sprite('imgs/lander.png')
    moon_struct = parse_background('imgs/background.png', struct_name='MoonData')
    star_struct = parse_background('imgs/starfield.png', max_tiles = 80, struct_name='StarData')

    outfile = open('generated_assets.hpp', 'w')
    outfile.write('#include <glm/gtc/type_ptr.hpp>\n')
    outfile.write('#include "PPU466.hpp"\n')
    
    outfile.write(lander_struct)
    outfile.write(moon_struct)
    outfile.write(star_struct)

    # read_sprite('imgs/lander_drawn.png.sprite')