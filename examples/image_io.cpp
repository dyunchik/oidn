// Copyright 2009-2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <fstream>
#include "image_io.h"

#if defined(OIDN_USE_OPENIMAGEIO)
  #include <OpenImageIO/imageio.h>
#endif

namespace oidn {

  namespace
  {
    inline float srgbForward(float y)
    {
      return (y <= 0.0031308f) ? (12.92f * y) : (1.055f * std::pow(y, 1.f/2.4f) - 0.055f);
    }

    inline float srgbInverse(float x)
    {
      return (x <= 0.04045f) ? (x / 12.92f) : std::pow((x + 0.055f) / 1.055f, 2.4f);
    }

    void srgbForward(ImageBuffer& image)
    {
      for (int i = 0; i < image.getDataSize(); ++i)
        image[i] = srgbForward(image[i]);
    }

    void srgbInverse(ImageBuffer& image)
    {
      for (int i = 0; i < image.getDataSize(); ++i)
        image[i] = srgbInverse(image[i]);
    }

    std::string getExtension(const std::string& filename)
    {
      const size_t pos = filename.find_last_of('.');
      if (pos == std::string::npos)
        return ""; // no extension
      else
      {
        std::string ext = filename.substr(pos + 1);
        for (auto& c : ext) c = tolower(c);
        return ext;
      }
    }

    ImageBuffer loadImagePFM(const std::string& filename)
    {
      // Open the file
      std::ifstream file(filename, std::ios::binary);
      if (file.fail())
        throw std::runtime_error("cannot open image file: " + filename);

      // Read the header
      std::string id;
      file >> id;
      int C;
      if (id == "PF")
        C = 3;
      else if (id == "Pf")
        C = 1;
      else
        throw std::runtime_error("invalid PFM image");

      int H, W;
      file >> W >> H;

      float scale;
      file >> scale;

      file.get(); // skip newline

      if (file.fail())
        throw std::runtime_error("invalid PFM image");

      if (scale >= 0.f)
        throw std::runtime_error("big-endian PFM images are not supported");
      scale = fabs(scale);

      // Read the pixels
      ImageBuffer image(W, H, C);

      for (int h = 0; h < H; ++h)
      {
        for (int w = 0; w < W; ++w)
        {
          for (int c = 0; c < C; ++c)
          {
            float x;
            file.read((char*)&x, sizeof(float));
            image[((H-1-h)*W + w) * C + c] = x * scale;
          }
        }
      }

      if (file.fail())
        throw std::runtime_error("invalid PFM image");

      return image;
    }

    void saveImagePFM(const std::string& filename, const ImageBuffer& image)
    {
      const int H = image.getHeight();
      const int W = image.getWidth();
      const int C = image.getChannels();

      // Open the file
      std::ofstream file(filename, std::ios::binary);
      if (file.fail())
        throw std::runtime_error("cannot open image file: " + filename);

      // Write the header
      file << "PF" << std::endl;
      file << W << " " << H << std::endl;
      file << "-1.0" << std::endl;

      // Write the pixels
      for (int h = 0; h < H; ++h)
      {
        for (int w = 0; w < W; ++w)
        {
          for (int c = 0; c < 3; ++c)
          {
            const float x = image[((H-1-h)*W + w) * C + c];
            file.write((char*)&x, sizeof(float));
          }
        }
      }
    }

    void saveImagePPM(const std::string& filename, const ImageBuffer& image)
    {
      if (image.getChannels() != 3)
        throw std::invalid_argument("image must have 3 channels");
      const int H = image.getHeight();
      const int W = image.getWidth();
      const int C = image.getChannels();

      // Open the file
      std::ofstream file(filename, std::ios::binary);
      if (file.fail())
        throw std::runtime_error("cannot open image file: " + filename);

      // Write the header
      file << "P6" << std::endl;
      file << W << " " << H << std::endl;
      file << "255" << std::endl;

      // Write the pixels
      for (int i = 0; i < W*H; ++i)
      {
        for (int c = 0; c < 3; ++c)
        {
          const float x = image[i*C+c];
          const int ch = std::min(std::max(int(x * 255.f), 0), 255);
          file.put(char(ch));
        }
      }
    }
  }

#ifdef OIDN_USE_OPENIMAGEIO
  ImageBuffer loadImageOIIO(const std::string& filename)
  {
    ImageBuffer buf;
    auto in = OIIO::ImageInput::open(filename);
    if (!in)
      throw std::runtime_error("cannot open image file: " + filename);

    const OIIO::ImageSpec& spec = in->spec();
    buf = ImageBuffer(spec.width, spec.height, spec.nchannels);
    if (!in->read_image(OIIO::TypeDesc::FLOAT, buf.getData()))
      throw std::runtime_error("failed to read image data");
    in->close();

#if OIIO_VERSION < 10903
    OIIO::ImageInput::destroy(in);
#endif
    return buf;
  }

  void saveImageOIIO(const std::string& filename, const ImageBuffer& image)
  {
    auto out = OIIO::ImageOutput::create(filename);
    if (!out)
      throw std::runtime_error("cannot save unsupported image file format: " + filename);

    OIIO::ImageSpec spec(image.getWidth(),
                         image.getHeight(),
                         image.getChannels(),
                         OIIO::TypeDesc::FLOAT);

    if (!out->open(filename, spec))
      throw std::runtime_error("cannot create image file: " + filename);
    if (!out->write_image(OIIO::TypeDesc::FLOAT, image.getData()))
      throw std::runtime_error("failed to write image data");
    out->close();

#if OIIO_VERSION < 10903
    OIIO::ImageOutput::destroy(out);
#endif
  }
#endif

  ImageBuffer loadImage(const std::string& filename)
  {
    const std::string ext = getExtension(filename);
    ImageBuffer image;

    if (ext == "pfm")
      image = loadImagePFM(filename);
    else
#if OIDN_USE_OPENIMAGEIO
      image = loadImageOIIO(filename);
#else
      throw std::runtime_error("cannot load unsupported image file format: " + filename);
#endif

    return image;
  }

  void saveImage(const std::string& filename, const ImageBuffer& image)
  {
    const std::string ext = getExtension(filename);
    if (ext == "pfm")
      saveImagePFM(filename, image);
    else if (ext == "ppm")
      saveImagePPM(filename, image);
    else
#if OIDN_USE_OPENIMAGEIO
      saveImageOIIO(filename, image);
#else
      throw std::runtime_error("cannot write unsupported image file format: " + filename);
#endif
  }

  bool isSrgbImage(const std::string& filename)
  {
    const std::string ext = getExtension(filename);
    return ext != "pfm" && ext != "exr" && ext != "hdr";
  }

  ImageBuffer loadImage(const std::string& filename, bool srgb)
  {
    ImageBuffer image = loadImage(filename);
    if (!srgb && isSrgbImage(filename))
      srgbInverse(image);
    return image;
  }

  void saveImage(const std::string& filename, const ImageBuffer& image, bool srgb)
  {
    if (!srgb && isSrgbImage(filename))
    {
      ImageBuffer newImage = image;
      srgbForward(newImage);
      saveImage(filename, newImage);
    }
    else
    {
      saveImage(filename, image);
    }
  }

} // namespace oidn
