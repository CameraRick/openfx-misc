/*
 OFX Chroma Keyer plugin.
 
 Copyright (C) 2014 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

#include "ChromaKeyer.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "../include/ofxsProcessing.H"

/*
  Simple Chroma Keyer.

  Algorithm description:
  [1] Keith Jack, "Video Demystified", Independent Pub Group (Computer), 1996, pp. 214-222, http://www.ee-techs.com/circuit/video-demy5.pdf

 A simplified version is described in:
  [2] High Quality Chroma Key, Michael Ashikhmin, http://www.cs.utah.edu/~michael/chroma/
*/

#define kKeyColorParamName "Key Color"
#define kKeyColorParamHint "Foreground key color; foreground areas containing the key color are replaced with the background image."

#define kAcceptanceAngleParamName "Acceptance Angle"
#define kAcceptanceAngleParamHint "Foreground colors are only suppressed inside the acceptance angle (alpha)."

#define kSuppressionAngleParamName "Suppression Angle"
#define kSuppressionAngleParamHint "The chrominance of foreground colors inside the suppression angle (beta) is set to zero on output, to deal with noise. Use no more than one third of acceptance angle."

#define kOutputModeParamName "Output Mode"
#define kOutputModeIntermediateOption "Intermediate"
#define kOutputModeIntermediateHint "Color is the source color. Alpha is the foreground key. Use for multi-pass keying."
#define kOutputModePremultipliedOption "Premultiplied"
#define kOutputModePremultipliedHint "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key."
#define kOutputModeUnpremultipliedOption "Unpremultiplied"
#define kOutputModeUnpremultipliedHint "Color is the Source color after key color suppression. Alpha is the foreground key."
#define kOutputModeCompositeOption "Composite"
#define kOutputModeCompositeHint "Color is the composite of Source and Bg. Alpha is the foreground key."

#define kSourceAlphaParamName "Source Alpha"
#define kSourceAlphaIgnoreOption "Ignore"
#define kSourceAlphaIgnoreHint "Ignore the source alpha."
#define kSourceAlphaAddToInsideMaskOption "Add to Inside Mask"
#define kSourceAlphaAddToInsideMaskHint "Source alpha is added to the inside mask."
#define kSourceAlphaNormalOption "Normal"
#define kSourceAlphaNormalHint "Foreground key is multiplied by source alpha."

#define kBgClipName "Bg"
#define kInsideMaskClipName "InM"
#define kOutsideMaskClipName "OutM"

enum OutputModeEnum {
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    eOutputModeComposite,
};

enum SourceAlphaEnum {
    eSourceAlphaIgnore,
    eSourceAlphaAddToInsideMask,
    eSourceAlphaNormal,
};

using namespace OFX;

class ChromaKeyerProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_bgImg;
    OFX::Image *_inMaskImg;
    OFX::Image *_outMaskImg;
    OfxRGBColourD _keyColor;
    double _acceptanceAngle;
    double _tan_acceptanceAngle_2;
    double _suppressionAngle;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;
    double _sinKey, _cosKey;

public:
    
    ChromaKeyerProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _bgImg(0)
    , _inMaskImg(0)
    , _outMaskImg(0)
    , _acceptanceAngle(0.)
    , _tan_acceptanceAngle_2(0.)
    , _suppressionAngle(0.)
    , _outputMode(eOutputModeComposite)
    , _sourceAlpha(eSourceAlphaIgnore)
    , _sinKey(0)
    , _cosKey(0)
    {
        
    }
    
    void setSrcImgs(OFX::Image *srcImg, OFX::Image *bgImg, OFX::Image *inMaskImg, OFX::Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }
    
    void setValues(const OfxRGBColourD& keyColor, double acceptanceAngle, double suppressionAngle, OutputModeEnum outputMode, SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _acceptanceAngle = acceptanceAngle;
        _suppressionAngle = suppressionAngle;
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
        double y, cb, cr;
        rgb2ycbcr(keyColor.r, keyColor.g, keyColor.b, &y, &cb, &cr);
        if (cb == 0. && cr == 0.) {
            cb = 1.;
        }
        double norm = std::sqrt(cb*cb + cr*cr);
        _cosKey = cb/norm;
        _sinKey = cr/norm;
        _tan_acceptanceAngle_2 = std::tan(_acceptanceAngle/2);
    }

    void rgb2ycbcr(double r, double g, double b, double *y, double *cb, double *cr)
    {
        *y = 0.2627*r+0.6780*g+0.0593*b;
        *cb = (b-*y)/1.8814;
        *cr = (r-*y)/1.4746;
    }
};



template <class PIX, int nComponents, int maxValue>
class ChromaKeyerProcessor : public ChromaKeyerProcessorBase
{
public :
    ChromaKeyerProcessor(OFX::ImageEffect &instance)
    : ChromaKeyerProcessorBase(instance)
    {
        
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                PIX *bgPix = (PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                PIX *inMaskPix = (PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                PIX *outMaskPix = (PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);

                float inMask = inMaskPix ? *inMaskPix : 0.;
                if (_sourceAlpha == eSourceAlphaAddToInsideMask) {
                    // take the max of inMask and the source Alpha
#pragma message ("TODO")
                    //TODO
                }
                float outMask = outMaskPix ? *outMaskPix : 0.;
                float Kbg = 0.;

                // clamp inMask and outMask in the [0,1] range
                inMask = std::max(0.f,std::min(inMask,1.f));
                outMask = std::max(0.f,std::min(outMask,1.f));

                // output of the foreground suppressor
                double fgr = 0.;
                double fgg = 0.;
                double fgb = 0.;

                if (!srcPix) {
                    // no source, take only background
                    Kbg = 1.;
                } else if (!bgPix) {
                    // no background, take source only
                    Kbg = 0.;
                } else if (outMask >= 1.-inMask) {
                    // outside mask has priority over inside mask
                    // (or outMask == 1)
                    Kbg = 1.;
                } else if (inMask >= 1) {
                    Kbg = 0.;
                } else {
                    // general case: compute Kbg from [1]

                    // first, we need to compute YCbCr coordinates.

                    // from Rec.2020  http://www.itu.int/rec/R-REC-BT.2020-0-201208-I/en :
                    // Y' = 0.2627R' + 0.6780G' + 0.0593B'
                    // Cb' = (B'-Y')/1.8814
                    // Cr' = (R'-Y')/1.4746
                    //
                    // or the "constant luminance" version
                    // Yc' = (0.2627R + 0.6780G + 0.0593B)'
                    // Cbc' = (B'-Yc')/1.9404 if -0.9702<=(B'-Y')<=0
                    //        (B'-Yc')/1.5816 if 0<=(R'-Y')<=0.7908
                    // Crc' = (R'-Yc')/1.7184 if -0.8592<=(B'-Y')<=0
                    //        (R'-Yc')/0.9936 if 0<=(R'-Y')<=0.4968
                    //
                    // with
                    // E' = 4.5E if 0 <=E<=beta
                    //      alpha*E^(0.45)-(alpha-1) if beta<=E<=1
                    // α = 1.099 and β = 0.018 for 10-bit system
                    // α = 1.0993 and β = 0.0181 for 12-bit system
                    //
                    // For our purpose, we only work in the linear space (which is why
                    // we don't allow UByte bit depth), and use the first set of formulas
                    //

                    double r = srcPix[0];
                    double g = srcPix[1];
                    double b = srcPix[2];
                    double y = 0.2627*r+0.6780*g+0.0593*b;
                    double fgy = y;
                    double fgcb = (b-y)/1.8814;
                    double fgcr = (r-y)/1.4746;
                    r = bgPix[0];
                    g = bgPix[1];
                    b = bgPix[2];
                    y = 0.2627*r+0.6780*g+0.0593*b;
                    double bgy = y;
                    double bgcb = (b-y)/1.8814;
                    double bgcr = (r-y)/1.4746;

                    ///////////////////////
                    // STEP A: Key Generator

                    // First, we rotate (Cb, Cr) coordinate system by an angle defined by the key color to obtain (X,Z) coordinate system.

                    // normalize fgcb and fgcr (which are in [-0.5,0.5]) to the [-1,1] interval
                    double fgcbp = fgcb * 2;
                    double fgcrp = fgcr * 2;

                    /* Convert foreground to XZ coords where X direction is defined by
                     the key color */

                    double fgx = _cosKey * fgcbp + _sinKey * fgcrp;
                    double fgz = -_sinKey * fgcbp + _cosKey * fgcrp;
                    // Since Cb ́ and Cr ́ are normalized to have a range of ±1, X and Z have a range of ±1.

                    // Second, we use a parameter alfa (60 to 120 degrees were used for different images) to divide the color space into two regions, one where the processing will be applied and the one where foreground will not be changed (where Kbg = 0 and blue_backing_contrubution = 0 in eq.1 above).
                    /* WARNING: accept angle should never be set greater than "somewhat less
                     than 90 degrees" to avoid dealing with negative/infinite tg. In reality,
                     80 degrees should be enough if foreground is reasonable. If this seems
                     to be a problem, go to alternative ways of checking point position
                     (scalar product or line equations). This angle should not be too small
                     either to avoid infinite ctg (used to suppress foreground without use of
                     division)*/

                    double Kfg;

                    if (fgx <= 0 || std::abs(fgz)/fgx > _tan_acceptanceAngle_2) {
                        /* keep foreground Kfg = 0*/
                        Kfg = 0.;
                    } else {
                        Kfg = fgx - std::abs(fgz)/_tan_acceptanceAngle_2;
                        //TODO
                    }

                    ///////////////
                    // STEP B: Nonadditive Mix

                    // nonadditive mix between the key generator and the garbage matte (outMask)

                    // The garbage matte is added to the foreground key signal (KFG) using a non-additive mixer (NAM). A nonadditive mixer takes the brighter of the two pictures, on a sample-by-sample basis, to generate the key signal. Matting is ideal for any source that generates its own keying signal, such as character generators, and so on.

                    // outside mask has priority over inside mask, treat inside first

                    if (Kfg < 1.-inMask) {
                        Kfg = std::max(0., 1.-inMask);
                    }
                    if (Kfg < outMask) {
                        Kfg = outMask;
                    }


                    //TODO

                    //////////////////////
                    // STEP C: Foreground suppressor

                    // The foreground suppressor reduces foreground color information by implementing X = X – KFG, with the key color being clamped to the black level.

                    /////////////////////
                    // STEP D: Key processor

                    // The key processor generates the initial background key signal (K ́BG) used to remove areas of the background image where the fore- ground is to be visible.

                    // Additional controls may be implemented to enable the foreground and background signals to be controlled independently. Examples are adjusting the contrast of the foreground so it matches the background or fading the fore- ground in various ways (such as fading to the background to make a foreground object van- ish or fading to black to generate a silhouette).
                    // In the computer environment, there may be relatively slow, smooth edges—especially edges involving smooth shading. As smooth edges are easily distorted during the chroma keying process, a wide keying process is usu- ally used in these circumstances. During wide keying, the keying signal starts before the edge of the graphic object.
                }

                // At this point, we have Kbg,

                // set the alpha channel to the opposite of Kbg
                dstPix[3] = 1. - Kbg;
                switch (_outputMode) {
                    case eOutputModeIntermediate:
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = srcPix[c];
                        }
                        break;
                    case eOutputModePremultiplied:
                        dstPix[0] = fgr * dstPix[3];
                        dstPix[1] = fgg * dstPix[3];
                        dstPix[2] = fgb * dstPix[3];
                        break;
                    case eOutputModeUnpremultiplied:
                        dstPix[0] = fgr;
                        dstPix[1] = fgg;
                        dstPix[2] = fgb;
                        break;
                    case eOutputModeComposite:
                        dstPix[0] = fgr * dstPix[3];
                        dstPix[1] = fgg * dstPix[3];
                        dstPix[2] = fgb * dstPix[3];
                        break;
                }
            }
        }
    }

};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ChromaKeyerPlugin : public OFX::ImageEffect
{
public :
    /** @brief ctor */
    ChromaKeyerPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , bgClip_(0)
    , inMaskClip_(0)
    , outMaskClip_(0)
    , keyColor_(0)
    , acceptanceAngle_(0)
    , suppressionAngle_(0)
    , outputMode_(0)
    , sourceAlpha_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        bgClip_ = fetchClip(kBgClipName);
        inMaskClip_ = fetchClip(kInsideMaskClipName);;
        outMaskClip_ = fetchClip(kOutsideMaskClipName);;
        keyColor_ = fetchRGBParam(kKeyColorParamName);
        acceptanceAngle_ = fetchDoubleParam(kAcceptanceAngleParamName);
        suppressionAngle_ = fetchDoubleParam(kSuppressionAngleParamName);
        outputMode_ = fetchChoiceParam(kOutputModeParamName);
        sourceAlpha_ = fetchChoiceParam(kSourceAlphaParamName);
    }
 
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(ChromaKeyerProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *bgClip_;
    OFX::Clip *inMaskClip_;
    OFX::Clip *outMaskClip_;
    
    OFX::RGBParam* keyColor_;
    OFX::DoubleParam* acceptanceAngle_;
    OFX::DoubleParam* suppressionAngle_;
    OFX::ChoiceParam* outputMode_;
    OFX::ChoiceParam* sourceAlpha_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ChromaKeyerPlugin::setupAndProcess(ChromaKeyerProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> bg(bgClip_->fetchImage(args.time));
    if(src.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    if(bg.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = bg->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = bg->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    // auto ptr for the masks.
    std::auto_ptr<OFX::Image> inMask(inMaskClip_ ? inMaskClip_->fetchImage(args.time) : 0);
    std::auto_ptr<OFX::Image> outMask(outMaskClip_ ? outMaskClip_->fetchImage(args.time) : 0);
    
    OfxRGBColourD keyColor;
    double acceptanceAngle;
    double suppressionAngle;
    int outputModeI;
    OutputModeEnum outputMode;
    int sourceAlphaI;
    SourceAlphaEnum sourceAlpha;
    keyColor_->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
    acceptanceAngle_->getValueAtTime(args.time, acceptanceAngle);
    suppressionAngle_->getValueAtTime(args.time, suppressionAngle);
    outputMode_->getValue(outputModeI);
    outputMode = (OutputModeEnum)outputModeI;
    sourceAlpha_->getValue(sourceAlphaI);
    sourceAlpha = (SourceAlphaEnum)sourceAlphaI;
    processor.setValues(keyColor, acceptanceAngle, suppressionAngle, outputMode, sourceAlpha);
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(), bg.get(), inMask.get(), outMask.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
ChromaKeyerPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if(dstComponents == OFX::ePixelComponentRGBA)
    {
        switch(dstBitDepth)
        {
            //case OFX::eBitDepthUByte :
            //{
            //    ChromaKeyerProcessor<unsigned char, 4, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        switch(dstBitDepth)
        {
            //case OFX::eBitDepthUByte :
            //{
            //    ChromaKeyerProcessor<unsigned char, 3, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


void ChromaKeyerPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ChromaKeyerOFX", "ChromaKeyerOFX", "ChromaKeyerOFX");
    desc.setPluginGrouping("Keyer");
    desc.setPluginDescription("Apply chroma keying");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}


void ChromaKeyerPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setOptional(false);
    
    ClipDescriptor* bgClip = desc.defineClip(kBgClipName);
    bgClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    bgClip->addSupportedComponent( OFX::ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(true);
    bgClip->setOptional(true);

   // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);
    
    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kInsideMaskClipName);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(true);
    inMaskClip->setIsMask(true);
    
    ClipDescriptor *outMaskClip =  desc.defineClip(kOutsideMaskClipName);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(true);
    outMaskClip->setIsMask(true);
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
 
    RGBParamDescriptor* keyColor = desc.defineRGBParam(kKeyColorParamName);
    keyColor->setLabels(kKeyColorParamName, kKeyColorParamName, kKeyColorParamName);
    keyColor->setHint(kKeyColorParamHint);
    keyColor->setDefault(0.,1.,0.);
    keyColor->setAnimates(true);
    page->addChild(*keyColor);
    
    DoubleParamDescriptor* acceptanceAngle = desc.defineDoubleParam(kAcceptanceAngleParamName);
    acceptanceAngle->setLabels(kAcceptanceAngleParamName, kAcceptanceAngleParamName, kAcceptanceAngleParamName);
    acceptanceAngle->setHint(kAcceptanceAngleParamHint);
    acceptanceAngle->setDoubleType(eDoubleTypeAngle);;
    acceptanceAngle->setRange(0., 175.);
    acceptanceAngle->setDefault(90.);
    acceptanceAngle->setAnimates(true);
    page->addChild(*acceptanceAngle);

    DoubleParamDescriptor* suppressionAngle = desc.defineDoubleParam(kSuppressionAngleParamName);
    suppressionAngle->setLabels(kSuppressionAngleParamName, kSuppressionAngleParamName, kSuppressionAngleParamName);
    suppressionAngle->setHint(kSuppressionAngleParamHint);
    suppressionAngle->setDoubleType(eDoubleTypeAngle);;
    suppressionAngle->setRange(0., 175.);
    suppressionAngle->setDefault(10.);
    suppressionAngle->setAnimates(true);
    page->addChild(*suppressionAngle);

    ChoiceParamDescriptor* outputMode = desc.defineChoiceParam(kOutputModeParamName);
    outputMode->setLabels(kOutputModeParamName, kOutputModeParamName, kOutputModeParamName);
    outputMode->appendOption(kOutputModeIntermediateOption, kOutputModeIntermediateHint);
    assert(outputMode->getNOptions() == (int)eOutputModeIntermediate);
    outputMode->appendOption(kOutputModePremultipliedOption, kOutputModePremultipliedHint);
    assert(outputMode->getNOptions() == (int)eOutputModePremultiplied);
    outputMode->appendOption(kOutputModeUnpremultipliedOption, kOutputModeUnpremultipliedHint);
    assert(outputMode->getNOptions() == (int)eOutputModeUnpremultiplied);
    outputMode->appendOption(kOutputModeCompositeOption, kOutputModeCompositeHint);
    assert(outputMode->getNOptions() == (int)eOutputModeComposite);
    outputMode->setDefault((int)eOutputModeComposite);
    page->addChild(*outputMode);

    ChoiceParamDescriptor* sourceAlpha = desc.defineChoiceParam(kSourceAlphaParamName);
    sourceAlpha->setLabels(kSourceAlphaParamName, kSourceAlphaParamName, kSourceAlphaParamName);
    sourceAlpha->appendOption(kSourceAlphaIgnoreOption, kSourceAlphaIgnoreHint);
    assert(sourceAlpha->getNOptions() == (int)eSourceAlphaIgnore);
    sourceAlpha->appendOption(kSourceAlphaAddToInsideMaskOption, kSourceAlphaAddToInsideMaskHint);
    assert(sourceAlpha->getNOptions() == (int)eSourceAlphaAddToInsideMask);
    sourceAlpha->appendOption(kSourceAlphaNormalOption, kSourceAlphaNormalHint);
    assert(sourceAlpha->getNOptions() == (int)eSourceAlphaNormal);
    sourceAlpha->setDefault((int)eSourceAlphaIgnore);
    page->addChild(*sourceAlpha);
}

OFX::ImageEffect* ChromaKeyerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ChromaKeyerPlugin(handle);
}
