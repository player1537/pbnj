#include "Camera.h"
#include "Renderer.h"
#include "Volume.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>

#include <stdlib.h>
#include <stdio.h>

#include <ospray/ospray.h>

#include "lodepng/lodepng.h"

namespace pbnj {

Renderer::Renderer() :
    backgroundColor(), samples(1)
{
    this->oRenderer = ospNewRenderer("scivis");

    this->setBackgroundColor(0, 0, 0);
    this->oCamera = NULL;
    this->oModel = NULL;
    this->oSurface = NULL;
    this->oMaterial = NULL;
    this->lastVolumeID = "unset";
    this->lastCameraID = "unset";
}

Renderer::~Renderer()
{
    ospRelease(this->oRenderer);
    ospRelease(this->oCamera);
    ospRelease(this->oModel);
    ospRelease(this->oSurface);
    ospRelease(this->oMaterial);
}

void Renderer::setBackgroundColor(unsigned char r, unsigned char g, unsigned char b)
{
    this->backgroundColor[0] = r;
    this->backgroundColor[1] = g;
    this->backgroundColor[2] = b;
    float asVec[] = {r/(float)255.0, g/(float)255.0, b/(float)255.0};
    ospSet3fv(this->oRenderer, "bgColor", asVec);
    ospCommit(this->oRenderer);
}

void Renderer::setBackgroundColor(std::vector<unsigned char> bgColor)
{
    // if the incoming vector is empty, it's probably from the config
    // just set to black instead
    if(bgColor.empty())
        this->setBackgroundColor(0, 0, 0);
    else
        this->setBackgroundColor(bgColor[0], bgColor[1], bgColor[2]);
}

void Renderer::setVolume(Volume *v)
{
    if(this->lastVolumeID == v->ID && this->lastRenderType == "volume") {
        // this is the same volume as the current model and we previously
        // did a volume render
        return;
    }
    if(this->oModel != NULL) {
        ospRelease(this->oModel);
        this->oModel = NULL;
    }

    this->lastVolumeID = v->ID;
    this->lastRenderType = "volume";
    this->oModel = ospNewModel();
    ospAddVolume(this->oModel, v->asOSPRayObject());
    ospCommit(this->oModel);
}

void Renderer::setIsosurface(Volume *v, std::vector<float> &isoValues)
{
    if(this->lastVolumeID == v->ID && this->lastRenderType == "isosurface") {
        // this is the same volume as the current model and we previously
        // did an isosurface render

        // but check if the isoValues are different
        if(this->lastIsoValues == isoValues) {
            return;
        }
    }
    if(this->oModel != NULL) {
        ospRelease(this->oModel);
        this->oModel = NULL;
    }

    // set up lights and material if necessary
    if(this->lights.size() == 0) {
        // create a new directional light
        OSPLight light = ospNewLight(this->oRenderer, "distant");
        float direction[] = {0, -1, 1};
        ospSet3fv(light, "direction", direction);
        // set the apparent size of the light in degrees
        // 0.53 approximates the Sun
        // however this doesn't seem to change anything!
        ospSet1f(light, "angularDiameter", 0.53);
        ospCommit(light);
        this->lights.push_back(light);
    }
    if(this->oMaterial == NULL) {
        // create a new surface material with some specular highlighting
        this->oMaterial = ospNewMaterial(this->oRenderer, "OBJMaterial");
        float diffuse[] = {1.0, 1.0, 1.0};
        float specular[] = {0.05, 0.05, 0.05};
        ospSet3fv(this->oMaterial, "Kd", diffuse);
        ospSet3fv(this->oMaterial, "Ks", specular);
        ospSet1f(this->oMaterial, "Ns", 10);
        ospCommit(this->oMaterial);
    }
    OSPData lightDataArray = ospNewData(this->lights.size(), OSP_LIGHT, this->lights.data());
    ospCommit(lightDataArray);
    ospSetObject(this->oRenderer, "lights", lightDataArray);
    unsigned int aoSamples = std::max(this->samples/8, (unsigned int) 1);
    ospSet1i(this->oRenderer, "aoSamples", aoSamples);
    ospSet1i(this->oRenderer, "shadowsEnabled", 0);
    ospSet1i(this->oRenderer, "oneSidedLighting", 0);
    ospCommit(this->oRenderer);

    // create an isosurface object
    if(this->oSurface != NULL) {
        ospRelease(this->oSurface);
        this->oSurface = NULL;
    }
    this->oSurface = ospNewGeometry("isosurfaces");
    OSPData isoValuesDataArray = ospNewData(isoValues.size(), OSP_FLOAT,
            isoValues.data());
    ospSetData(this->oSurface, "isovalues", isoValuesDataArray);
    ospSetObject(this->oSurface, "volume", v->asOSPRayObject());
    ospSetMaterial(this->oSurface, this->oMaterial);
    ospCommit(this->oSurface);

    this->lastVolumeID = v->ID;
    this->lastRenderType = "isosurface";
    this->lastIsoValues = isoValues;
    this->oModel = ospNewModel();
    ospAddGeometry(this->oModel, this->oSurface);
    ospCommit(this->oModel);
}

void Renderer::setCamera(Camera *c)
{
    if(this->lastCameraID == c->ID) {
        // this is the same camera as the current one
        return;
    }
    if(this->oCamera != NULL) {
        //std::cerr << "Camera already set!" << std::endl;
        //return;
        ospRelease(this->oCamera);
        this->oCamera = NULL;
    }

    this->lastCameraID = c->ID;
    this->cameraWidth = c->imageWidth;
    this->cameraHeight = c->imageHeight;
    this->oCamera = c->asOSPRayObject();
}

void Renderer::setSamples(unsigned int spp)
{
    this->samples = spp;
    ospSet1i(this->oRenderer, "spp", spp);
    ospCommit(this->oRenderer);
}

void Renderer::renderImage(std::string imageFilename)
{
    IMAGETYPE imageType = this->getFiletype(imageFilename);
    if(imageType == INVALID) {
        std::cerr << "Invalid image filetype requested!" << std::endl;
        return;
    }

    this->render();
    this->saveImage(imageFilename, imageType);
}

void Renderer::renderToPNGObject(std::vector<unsigned char> &png)
{
    unsigned char *colorBuffer;
    this->renderToBuffer(&colorBuffer);
    unsigned int error = lodepng::encode(png, colorBuffer,
            this->cameraWidth, this->cameraHeight);
    if(error) {
        std::cerr << "ERROR: could not encode PNG, error " << error << ": ";
        std::cerr << lodepng_error_text(error) << std::endl;
    }
    free(colorBuffer);
}

/*
 * Renders the OSPRay buffer to buffer and sets the width and height in 
 * their respective variables.
 */
void Renderer::renderToBuffer(unsigned char **buffer)
{
    this->render();
    int width = this->cameraWidth;
    int height = this->cameraHeight;
    uint32_t *colorBuffer = (uint32_t *)ospMapFrameBuffer(this->oFrameBuffer,
            OSP_FB_COLOR);
    
    *buffer = (unsigned char *) malloc(4 * width * height);
    
    for(int j = 0; j < height; j++) {
        unsigned char *rowIn = (unsigned char*)&colorBuffer[(height-1-j)*width];
        for(int i = 0; i < width; i++) {
            int index = j * width + i;
            // composite rowIn RGB with background color
            unsigned char r = rowIn[4*i + 0],
                          g = rowIn[4*i + 1],
                          b = rowIn[4*i + 2];
            float a = rowIn[4*i + 3] / 255.0;
            (*buffer)[4*index + 0] = (unsigned char) r * a +
                this->backgroundColor[0] * (1.0-a);
            (*buffer)[4*index + 1] = (unsigned char) g * a +
                this->backgroundColor[1] * (1.0-a);
            (*buffer)[4*index + 2] = (unsigned char) b * a +
                this->backgroundColor[2] * (1.0-a);
            (*buffer)[4*index + 3] = 255;
        }
    }

    ospUnmapFrameBuffer(colorBuffer, this->oFrameBuffer);
    ospRelease(this->oFrameBuffer);
}

void Renderer::render()
{
    //check if everything is ready for rendering
    bool exit = false;
    if(this->oModel == NULL) {
        std::cerr << "No volume set to render!" << std::endl;
        exit = true;
    }
    if(this->oCamera == NULL) {
        std::cerr << "No camera set to render with!" << std::endl;
        exit = true;
    }
    if(exit)
        return;

    //finalize the OSPRay renderer
    ospSetObject(this->oRenderer, "model", this->oModel);
    ospSetObject(this->oRenderer, "camera", this->oCamera);
    ospCommit(this->oRenderer);

    //set up framebuffer
    osp::vec2i imageSize;
    imageSize.x = this->cameraWidth;
    imageSize.y = this->cameraHeight;
    //this framebuffer will be released after a single frame
    this->oFrameBuffer = ospNewFrameBuffer(imageSize, OSP_FB_SRGBA,
                                           OSP_FB_COLOR | OSP_FB_ACCUM);
    ospRenderFrame(this->oFrameBuffer, this->oRenderer,
            OSP_FB_COLOR | OSP_FB_ACCUM);

}

IMAGETYPE Renderer::getFiletype(std::string filename)
{
    std::stringstream ss;
    ss.str(filename);
    std::string token;
    char delim = '.';
    while(std::getline(ss, token, delim)) {
    }

    if(token.compare("ppm") == 0) {
        return PIXMAP;
    }
    else if(token.compare("png") == 0) {
        return PNG;
    }
    else {
        return INVALID;
    }
}

void Renderer::saveImage(std::string filename, IMAGETYPE imageType)
{
    if(imageType == PIXMAP)
        this->saveAsPPM(filename);
    else if(imageType == PNG)
        this->saveAsPNG(filename);
}

void Renderer::saveAsPPM(std::string filename)
{
    int width = this->cameraWidth, height = this->cameraHeight;
    uint32_t *colorBuffer = (uint32_t *)ospMapFrameBuffer(this->oFrameBuffer,
            OSP_FB_COLOR);
    //do a binary file so the PPM isn't quite so large
    FILE *file = fopen(filename.c_str(), "wb");
    unsigned char *rowOut = (unsigned char *)malloc(3*width);
    fprintf(file, "P6\n%i %i\n255\n", width, height);

    //the OSPRay framebuffer uses RGBA, but PPM only supports RGB
    for(int j = 0; j < height; j++) {
        unsigned char *rowIn = (unsigned char*)&colorBuffer[(height-1-j)*width];
        for(int i = 0; i < width; i++) {
            // composite rowIn RGB with background color
            unsigned char r = rowIn[4*i + 0],
                          g = rowIn[4*i + 1],
                          b = rowIn[4*i + 2];
            float a = rowIn[4*i + 3] / 255.0;
            rowOut[3*i + 0] = (unsigned char) r * a + 
                this->backgroundColor[0] * (1.0-a);
            rowOut[3*i + 1] = (unsigned char) g * a + 
                this->backgroundColor[1] * (1.0-a);
            rowOut[3*i + 2] = (unsigned char) b * a + 
                this->backgroundColor[2] * (1.0-a);
        }
        fwrite(rowOut, 3*width, sizeof(char), file);
    }

    fprintf(file, "\n");
    fclose(file);

    //unmap and release so OSPRay will deallocate the memory
    //used by the framebuffer
    ospUnmapFrameBuffer(colorBuffer, this->oFrameBuffer);
    ospRelease(this->oFrameBuffer);
}

void Renderer::saveAsPNG(std::string filename)
{
    std::vector<unsigned char> converted_image;
    this->renderToPNGObject(converted_image);
    //write to file
    lodepng::save_file(converted_image, filename.c_str());
}

}
