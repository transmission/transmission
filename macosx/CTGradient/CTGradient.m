//
//  CTGradient.m
//
//  Created by Chad Weider on 2/14/07.
//  Copyright (c) 2007 Chad Weider.
//  Some rights reserved: <http://creativecommons.org/licenses/by/2.5/>
//
//  Version: 1.6

#import "CTGradient.h"

@interface CTGradient (Private)
- (void)_commonInit;
- (void)setBlendingMode:(CTGradientBlendingMode)mode;
- (void)addElement:(CTGradientElement*)newElement;

- (CTGradientElement *)elementAtIndex:(unsigned)index;

- (CTGradientElement)removeElementAtIndex:(unsigned)index;
- (CTGradientElement)removeElementAtPosition:(float)position;
@end

//C Fuctions for color blending
static void linearEvaluation   (void *info, const float *in, float *out);
static void chromaticEvaluation(void *info, const float *in, float *out);
static void inverseChromaticEvaluation(void *info, const float *in, float *out);
static void transformRGB_HSV(float *components);
static void transformHSV_RGB(float *components);
static void resolveHSV(float *color1, float *color2);


@implementation CTGradient
/////////////////////////////////////Initialization Type Stuff
- (id)init
  {
  self = [super init];
  
  if (self != nil)
	{
	[self _commonInit];
	[self setBlendingMode:CTLinearBlendingMode];
	}
  return self;
  }

- (void)_commonInit
  {
  elementList = nil;
  }

- (void)dealloc
  {
  CGFunctionRelease(gradientFunction);
  
  CTGradientElement *elementToRemove = elementList;
  while(elementList != nil)
	{
	elementToRemove = elementList;
	elementList = elementList->nextElement;
	free(elementToRemove);
	}
  
  [super dealloc];
  }

- (id)copyWithZone:(NSZone *)zone
  {
  CTGradient *copy = [[[self class] allocWithZone:zone] init];
  
  //now just copy my elementlist
  CTGradientElement *currentElement = elementList;
  while(currentElement != nil)
	{
	[copy addElement:currentElement];
	currentElement = currentElement->nextElement;
	}
  
  [copy setBlendingMode:blendingMode];
  
  return copy;
  }

- (void)encodeWithCoder:(NSCoder *)coder
  {
  if([coder allowsKeyedCoding])
	{
	unsigned count = 0;
	CTGradientElement *currentElement = elementList;
	while(currentElement != nil)
		{
		[coder encodeValueOfObjCType:@encode(float) at:&(currentElement->red)];
		[coder encodeValueOfObjCType:@encode(float) at:&(currentElement->green)];
		[coder encodeValueOfObjCType:@encode(float) at:&(currentElement->blue)];
		[coder encodeValueOfObjCType:@encode(float) at:&(currentElement->alpha)];
		[coder encodeValueOfObjCType:@encode(float) at:&(currentElement->position)];
		
		count++;
		currentElement = currentElement->nextElement;
		}
	[coder encodeInt:count forKey:@"CTGradientElementCount"];
	[coder encodeInt:blendingMode forKey:@"CTGradientBlendingMode"];
	}
  else
	[NSException raise:NSInvalidArchiveOperationException format:@"Only supports NSKeyedArchiver coders"];
  }

- (id)initWithCoder:(NSCoder *)coder
  {
  [self _commonInit];
  
  [self setBlendingMode:[coder decodeIntForKey:@"CTGradientBlendingMode"]];
  unsigned count = [coder decodeIntForKey:@"CTGradientElementCount"];
  
  while(count != 0)
	{
    CTGradientElement newElement;
	
	[coder decodeValueOfObjCType:@encode(float) at:&(newElement.red)];
	[coder decodeValueOfObjCType:@encode(float) at:&(newElement.green)];
	[coder decodeValueOfObjCType:@encode(float) at:&(newElement.blue)];
	[coder decodeValueOfObjCType:@encode(float) at:&(newElement.alpha)];
	[coder decodeValueOfObjCType:@encode(float) at:&(newElement.position)];
	
	count--;
	[self addElement:&newElement];
	}
  return self;
  }
#pragma mark -



#pragma mark Creation
+ (id)gradientWithBeginningColor:(NSColor *)begin endingColor:(NSColor *)end
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  CTGradientElement color2;
  
  [[begin colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color1.red
															   green:&color1.green
																blue:&color1.blue
															   alpha:&color1.alpha];
  
  [[end   colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&color2.red
															   green:&color2.green
																blue:&color2.blue
															   alpha:&color2.alpha];  
  color1.position = 0;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)aquaSelectedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red   = 0.58;
  color1.green = 0.86;
  color1.blue  = 0.98;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red   = 0.42;
  color2.green = 0.68;
  color2.blue  = 0.90;
  color2.alpha = 1.00;
  color2.position = 11.5/23;
  
  CTGradientElement color3;
  color3.red   = 0.64;
  color3.green = 0.80;
  color3.blue  = 0.94;
  color3.alpha = 1.00;
  color3.position = 11.5/23;
  
  CTGradientElement color4;
  color4.red   = 0.56;
  color4.green = 0.70;
  color4.blue  = 0.90;
  color4.alpha = 1.00;
  color4.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  [newInstance addElement:&color3];
  [newInstance addElement:&color4];
  
  return [newInstance autorelease];
  }

+ (id)aquaNormalGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.95;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.83;
  color2.alpha = 1.00;
  color2.position = 11.5/23;
  
  CTGradientElement color3;
  color3.red = color3.green = color3.blue  = 0.95;
  color3.alpha = 1.00;
  color3.position = 11.5/23;
  
  CTGradientElement color4;
  color4.red = color4.green = color4.blue  = 0.92;
  color4.alpha = 1.00;
  color4.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  [newInstance addElement:&color3];
  [newInstance addElement:&color4];
  
  return [newInstance autorelease];
  }

+ (id)aquaPressedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.80;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.64;
  color2.alpha = 1.00;
  color2.position = 11.5/23;
  
  CTGradientElement color3;
  color3.red = color3.green = color3.blue  = 0.80;
  color3.alpha = 1.00;
  color3.position = 11.5/23;
  
  CTGradientElement color4;
  color4.red = color4.green = color4.blue  = 0.77;
  color4.alpha = 1.00;
  color4.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  [newInstance addElement:&color3];
  [newInstance addElement:&color4];
  
  return [newInstance autorelease];
  }

+ (id)unifiedSelectedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.85;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.95;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)unifiedNormalGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.75;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.90;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)unifiedPressedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.60;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.75;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)unifiedDarkGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red = color1.green = color1.blue  = 0.68;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red = color2.green = color2.blue  = 0.83;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)sourceListSelectedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red   = 0.06;
  color1.green = 0.37;
  color1.blue  = 0.85;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red   = 0.30;
  color2.green = 0.60;
  color2.blue  = 0.92;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)sourceListUnselectedGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red   = 0.43;
  color1.green = 0.43;
  color1.blue  = 0.43;
  color1.alpha = 1.00;
  color1.position = 0;
  
  CTGradientElement color2;
  color2.red   = 0.60;
  color2.green = 0.60;
  color2.blue  = 0.60;
  color2.alpha = 1.00;
  color2.position = 1;
  
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  return [newInstance autorelease];
  }

+ (id)rainbowGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement color1;
  color1.red   = 1.00;
  color1.green = 0.00;
  color1.blue  = 0.00;
  color1.alpha = 1.00;
  color1.position = 0.0;
  
  CTGradientElement color2;
  color2.red   = 0.54;
  color2.green = 0.00;
  color2.blue  = 1.00;
  color2.alpha = 1.00;
  color2.position = 1.0;
    
  [newInstance addElement:&color1];
  [newInstance addElement:&color2];
  
  [newInstance setBlendingMode:CTChromaticBlendingMode];
  
  return [newInstance autorelease];
  }

+ (id)hydrogenSpectrumGradient
  {
  id newInstance = [[[self class] alloc] init];
  
  struct {float hue; float position; float width;} colorBands[4];
  
  colorBands[0].hue = 22;
  colorBands[0].position = .145;
  colorBands[0].width = .01;
  
  colorBands[1].hue = 200;
  colorBands[1].position = .71;
  colorBands[1].width = .008;
  
  colorBands[2].hue = 253;
  colorBands[2].position = .885;
  colorBands[2].width = .005;
  
  colorBands[3].hue = 275;
  colorBands[3].position = .965;
  colorBands[3].width = .003;
  
  int i;
  /////////////////////////////
  for(i = 0; i < 4; i++)
	{	
	float color[4];
	color[0] = colorBands[i].hue - 180*colorBands[i].width;
	color[1] = 1;
	color[2] = 0.001;
	color[3] = 1;
	transformHSV_RGB(color);
	CTGradientElement fadeIn;
	fadeIn.red   = color[0];
	fadeIn.green = color[1];
	fadeIn.blue  = color[2];
	fadeIn.alpha = color[3];
	fadeIn.position = colorBands[i].position - colorBands[i].width;
	
	
	color[0] = colorBands[i].hue;
	color[1] = 1;
	color[2] = 1;
	color[3] = 1;
	transformHSV_RGB(color);
	CTGradientElement band;
	band.red   = color[0];
	band.green = color[1];
	band.blue  = color[2];
	band.alpha = color[3];
	band.position = colorBands[i].position;
	
	color[0] = colorBands[i].hue + 180*colorBands[i].width;
	color[1] = 1;
	color[2] = 0.001;
	color[3] = 1;
	transformHSV_RGB(color);
	CTGradientElement fadeOut;
	fadeOut.red   = color[0];
	fadeOut.green = color[1];
	fadeOut.blue  = color[2];
	fadeOut.alpha = color[3];
	fadeOut.position = colorBands[i].position + colorBands[i].width;
	
	
	[newInstance addElement:&fadeIn];
	[newInstance addElement:&band];
	[newInstance addElement:&fadeOut];
	}
  
  [newInstance setBlendingMode:CTChromaticBlendingMode];
  
  return [newInstance autorelease];
  }

#pragma mark -



#pragma mark Modification
- (CTGradient *)gradientWithAlphaComponent:(float)alpha
  {
  id newInstance = [[[self class] alloc] init];
  
  CTGradientElement *curElement = elementList;
  CTGradientElement tempElement;

  while(curElement != nil)
	{
	tempElement = *curElement;
	tempElement.alpha = alpha;
	[newInstance addElement:&tempElement];
	
	curElement = curElement->nextElement;
	}
  
  return [newInstance autorelease];
  }

- (CTGradient *)gradientWithBlendingMode:(CTGradientBlendingMode)mode
  {
  CTGradient *newGradient = [self copy];  
  
  [newGradient setBlendingMode:mode];
  
  return [newGradient autorelease];
  }


//Adds a color stop with <color> at <position> in elementList
//(if two elements are at the same position then added imediatly after the one that was there already)
- (CTGradient *)addColorStop:(NSColor *)color atPosition:(float)position
  {
  CTGradient *newGradient = [self copy];
  CTGradientElement newGradientElement;
  
  //put the components of color into the newGradientElement - must make sure it is a RGB color (not Gray or CMYK) 
  [[color colorUsingColorSpaceName:NSCalibratedRGBColorSpace] getRed:&newGradientElement.red
															   green:&newGradientElement.green
																blue:&newGradientElement.blue
															   alpha:&newGradientElement.alpha];
  newGradientElement.position = position;
  
  //Pass it off to addElement to take care of adding it to the elementList
  [newGradient addElement:&newGradientElement];
  
  return [newGradient autorelease];
  }


//Removes the color stop at <position> from elementList
- (CTGradient *)removeColorStopAtPosition:(float)position
  {
  CTGradient *newGradient = [self copy];
  CTGradientElement removedElement = [newGradient removeElementAtPosition:position];
  
  if(isnan(removedElement.position))
	[NSException raise:NSRangeException format:@"-[%@ removeColorStopAtPosition:]: no such colorStop at position (%f)", [self class], position];
  
  return [newGradient autorelease];
  }

- (CTGradient *)removeColorStopAtIndex:(unsigned)index
  {
  CTGradient *newGradient = [self copy];
  CTGradientElement removedElement = [newGradient removeElementAtIndex:index];
  
  if(isnan(removedElement.position))
	[NSException raise:NSRangeException format:@"-[%@ removeColorStopAtIndex:]: index (%i) beyond bounds", [self class], index];
  
  return [newGradient autorelease];
  }
#pragma mark -



#pragma mark Information
- (CTGradientBlendingMode)blendingMode
  {
  return blendingMode;
  }

//Returns color at <position> in gradient
- (NSColor *)colorStopAtIndex:(unsigned)index
  {
  CTGradientElement *element = [self elementAtIndex:index];
  
  if(element != nil)
	return [NSColor colorWithCalibratedRed:element->red 
									 green:element->green
									  blue:element->blue
									 alpha:element->alpha];
  
  [NSException raise:NSRangeException format:@"-[%@ removeColorStopAtIndex:]: index (%i) beyond bounds", [self class], index];
  
  return nil;
  }

- (NSColor *)colorAtPosition:(float)position
  {
  float components[4];
  
  switch(blendingMode)
	{
	case CTLinearBlendingMode:
		 linearEvaluation(&elementList, &position, components);				break;
	case CTChromaticBlendingMode:
		 chromaticEvaluation(&elementList, &position, components);			break;
	case CTInverseChromaticBlendingMode:
		 inverseChromaticEvaluation(&elementList, &position, components);	break;
	}
  
  
  return [NSColor colorWithCalibratedRed:components[0]
								   green:components[1]
								    blue:components[2]
								   alpha:components[3]];
  }
#pragma mark -



#pragma mark Drawing
- (void)drawSwatchInRect:(NSRect)rect
  {
  [self fillRect:rect angle:45];
  }

- (void)fillRect:(NSRect)rect angle:(float)angle
  {
  //First Calculate where the beginning and ending points should be
  CGPoint startPoint;
  CGPoint endPoint;
  
  if(angle == 0)		//screw the calculations - we know the answer
  	{
  	startPoint = CGPointMake(NSMinX(rect), NSMinY(rect));	//right of rect
  	endPoint   = CGPointMake(NSMaxX(rect), NSMinY(rect));	//left  of rect
  	}
  else if(angle == 90)	//same as above
  	{
  	startPoint = CGPointMake(NSMinX(rect), NSMinY(rect));	//bottom of rect
  	endPoint   = CGPointMake(NSMinX(rect), NSMaxY(rect));	//top    of rect
  	}
  else						//ok, we'll do the calculations now 
  	{
  	float x,y;
  	float sina, cosa, tana;
  	
  	float length;
  	float deltax,
  		  deltay;
	
  	float rangle = angle * pi/180;	//convert the angle to radians
	
  	if(fabsf(tan(rangle))<=1)	//for range [-45,45], [135,225]
		{
		x = NSWidth(rect);
		y = NSHeight(rect);
		
		sina = sin(rangle);
		cosa = cos(rangle);
		tana = tan(rangle);
		
		length = x/fabsf(cosa)+(y-x*fabsf(tana))*fabsf(sina);
		
		deltax = length*cosa/2;
		deltay = length*sina/2;
		}
	else						//for range [45,135], [225,315]
		{
		x = NSHeight(rect);
		y = NSWidth(rect);
		
		sina = sin(rangle - 90*pi/180);
		cosa = cos(rangle - 90*pi/180);
		tana = tan(rangle - 90*pi/180);
		
		length = x/fabsf(cosa)+(y-x*fabsf(tana))*fabsf(sina);
		
		deltax =-length*sina/2;
		deltay = length*cosa/2;
		}
  
	startPoint = CGPointMake(NSMidX(rect)-deltax, NSMidY(rect)-deltay);
	endPoint   = CGPointMake(NSMidX(rect)+deltax, NSMidY(rect)+deltay);
	}
  
  //Calls to CoreGraphics
  CGContextRef currentContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
  CGContextSaveGState(currentContext);
	  #if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
		CGColorSpaceRef colorspace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
	  #else
		CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
	  #endif
	  CGShadingRef myCGShading = CGShadingCreateAxial(colorspace, startPoint, endPoint, gradientFunction, false, false);
	  
	  CGContextClipToRect (currentContext, *(CGRect *)&rect);	//This is where the action happens
	  CGContextDrawShading(currentContext, myCGShading);
	  
	  CGShadingRelease(myCGShading);
	  CGColorSpaceRelease(colorspace );
  CGContextRestoreGState(currentContext);
  }

- (void)radialFillRect:(NSRect)rect
  {
  CGPoint startPoint , endPoint;
  float startRadius, endRadius;
  float scalex, scaley, transx, transy;
  
  startPoint = endPoint = CGPointMake(NSMidX(rect), NSMidY(rect));
  
  startRadius = -1;
  if(NSHeight(rect)>NSWidth(rect))
	{
	scalex = NSWidth(rect)/NSHeight(rect);
	transx = (NSHeight(rect)-NSWidth(rect))/2;
	scaley = 1;
	transy = 1;
	endRadius = NSHeight(rect)/2;
	}
  else
	{
	scalex = 1;
	transx = 1;
	scaley = NSHeight(rect)/NSWidth(rect);
	transy = (NSWidth(rect)-NSHeight(rect))/2;
	endRadius = NSWidth(rect)/2;
	}
  
  //Calls to CoreGraphics
  CGContextRef currentContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
  CGContextSaveGState(currentContext);
	  #if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
		CGColorSpaceRef colorspace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
	  #else
		CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
	  #endif
  	  CGShadingRef myCGShading = CGShadingCreateRadial(colorspace, startPoint, startRadius, endPoint, endRadius, gradientFunction, true, true);

	  CGContextClipToRect  (currentContext, *(CGRect *)&rect);
	  CGContextScaleCTM    (currentContext, scalex, scaley);
	  CGContextTranslateCTM(currentContext, transx, transy);
	  CGContextDrawShading (currentContext, myCGShading);		//This is where the action happens
	  
	  CGShadingRelease(myCGShading);
	  CGColorSpaceRelease(colorspace);
  CGContextRestoreGState(currentContext);
  }

- (void)fillBezierPath:(NSBezierPath *)path angle:(float)angle
  {
  NSGraphicsContext *currentContext = [NSGraphicsContext currentContext];
  [currentContext saveGraphicsState];
	NSAffineTransform *transform = [[NSAffineTransform alloc] init];
	
	[transform rotateByDegrees:-angle];
	[path transformUsingAffineTransform:transform];
	[transform invert];
	[transform concat];
	
	[path addClip];
	[self fillRect:[path bounds] angle:0];
	[path transformUsingAffineTransform:transform];
	[transform release];
  [currentContext restoreGraphicsState];
  }
- (void)radialFillBezierPath:(NSBezierPath *)path
  {
  NSGraphicsContext *currentContext = [NSGraphicsContext currentContext];
  [currentContext saveGraphicsState];
	[path addClip];
	[self radialFillRect:[path bounds]];
  [currentContext restoreGraphicsState];
  }
#pragma mark -



#pragma mark Private Methods
- (void)setBlendingMode:(CTGradientBlendingMode)mode;
  {
  blendingMode = mode;
  
  //Choose what blending function to use
  void *evaluationFunction;
  switch(blendingMode)
	{
	case CTLinearBlendingMode:
		 evaluationFunction = &linearEvaluation;			break;
	case CTChromaticBlendingMode:
		 evaluationFunction = &chromaticEvaluation;			break;
	case CTInverseChromaticBlendingMode:
		 evaluationFunction = &inverseChromaticEvaluation;	break;
	}
  
  //replace the current CoreGraphics Function with new one
  if(gradientFunction != NULL)
	  CGFunctionRelease(gradientFunction);
    
  CGFunctionCallbacks evaluationCallbackInfo = {0 , evaluationFunction, NULL};	//Version, evaluator function, cleanup function
  
  static const float input_value_range   [2] = { 0, 1 };						//range  for the evaluator input
  static const float output_value_ranges [8] = { 0, 1, 0, 1, 0, 1, 0, 1 };		//ranges for the evaluator output (4 returned values)
  
  gradientFunction = CGFunctionCreate(&elementList,					//the two transition colors
									  1, input_value_range  ,		//number of inputs (just fraction of progression)
									  4, output_value_ranges,		//number of outputs (4 - RGBa)
									  &evaluationCallbackInfo);		//info for using the evaluator function
  }

- (void)addElement:(CTGradientElement *)newElement
{
  if(elementList == nil || newElement->position < elementList->position)	//inserting at beginning of list
	{
	CTGradientElement *tmpNext = elementList;
	elementList = malloc(sizeof(CTGradientElement));
	*elementList = *newElement;
	elementList->nextElement = tmpNext;
	}
  else																		//inserting somewhere inside list
	{
	CTGradientElement *curElement = elementList;
	
	while(curElement->nextElement != nil && !((curElement->position <= newElement->position) && (newElement->position < curElement->nextElement->position)))
		{
		curElement = curElement->nextElement;
		}
	
	CTGradientElement *tmpNext = curElement->nextElement;
	curElement->nextElement = malloc(sizeof(CTGradientElement));
	*(curElement->nextElement) = *newElement;
	curElement->nextElement->nextElement = tmpNext;
	}
  }

- (CTGradientElement)removeElementAtIndex:(unsigned)index
  {
  CTGradientElement removedElement;
  
  if(elementList != nil)
	{
	if(index == 0)
		{
		CTGradientElement *tmpNext = elementList;
		elementList = elementList->nextElement;
		
		removedElement = *tmpNext;
		free(tmpNext);
		
		return removedElement;
		}
	
	unsigned count = 1;		//we want to start one ahead
	CTGradientElement *currentElement = elementList;
	while(currentElement->nextElement != nil)
		{
		if(count == index)
			{
			CTGradientElement *tmpNext  = currentElement->nextElement;
			currentElement->nextElement = currentElement->nextElement->nextElement;
			
			removedElement = *tmpNext;
			free(tmpNext);

			return removedElement;
			}

		count++;
		currentElement = currentElement->nextElement;
		}
	}
  
  //element is not found, return empty element
  removedElement.red   = 0.0;
  removedElement.green = 0.0;
  removedElement.blue  = 0.0;
  removedElement.alpha = 0.0;
  removedElement.position = NAN;
  removedElement.nextElement = nil;
  
  return removedElement;
  }

- (CTGradientElement)removeElementAtPosition:(float)position
  {
  CTGradientElement removedElement;
  
  if(elementList != nil)
	{
	if(elementList->position == position)
		{
		CTGradientElement *tmpNext = elementList;
		elementList = elementList->nextElement;
		
		removedElement = *tmpNext;
		free(tmpNext);
		
		return removedElement;
		}
	else
		{
		CTGradientElement *curElement = elementList;
		while(curElement->nextElement != nil)
			{
			if(curElement->nextElement->position == position)
				{
				CTGradientElement *tmpNext = curElement->nextElement;
				curElement->nextElement = curElement->nextElement->nextElement;
				
				removedElement = *tmpNext;
				free(tmpNext);

				return removedElement;
				}
			}
		}
	}
  
  //element is not found, return empty element
  removedElement.red   = 0.0;
  removedElement.green = 0.0;
  removedElement.blue  = 0.0;
  removedElement.alpha = 0.0;
  removedElement.position = NAN;
  removedElement.nextElement = nil;
  
  return removedElement;
  }


- (CTGradientElement *)elementAtIndex:(unsigned)index;			
  {
  unsigned count = 0;
  CTGradientElement *currentElement = elementList;
  
  while(currentElement != nil)
	{
	if(count == index)
		return currentElement;
	
	count++;
	currentElement = currentElement->nextElement;
	}
  
  return nil;
  }
#pragma mark -



#pragma mark Core Graphics
//////////////////////////////////////Blending Functions/////////////////////////////////////
void linearEvaluation (void *info, const float *in, float *out)
  {
  float position = *in;
  
  if(*(CTGradientElement **)info == nil)	//if elementList is empty return clear color
	{
	out[0] = out[1] = out[2] = out[3] = 1;
	return;
	}
  
  //This grabs the first two colors in the sequence
  CTGradientElement *color1 = *(CTGradientElement **)info;
  CTGradientElement *color2 = color1->nextElement;
  
  //make sure first color and second color are on other sides of position
  while(color2 != nil && color2->position < position)
  	{
  	color1 = color2;
  	color2 = color1->nextElement;
  	}
  //if we don't have another color then make next color the same color
  if(color2 == nil)
    {
	color2 = color1;
    }
  
  //----------FailSafe settings----------
  //color1->red   = 1; color2->red   = 0;
  //color1->green = 1; color2->green = 0;
  //color1->blue  = 1; color2->blue  = 0;
  //color1->alpha = 1; color2->alpha = 1;
  //color1->position = .5;
  //color2->position = .5;
  //-------------------------------------
  
  if(position <= color1->position)			//Make all below color color1's position equal to color1
  	{
  	out[0] = color1->red; 
  	out[1] = color1->green;
  	out[2] = color1->blue;
  	out[3] = color1->alpha;
  	}
  else if (position >= color2->position)	//Make all above color color2's position equal to color2
  	{
  	out[0] = color2->red; 
  	out[1] = color2->green;
  	out[2] = color2->blue;
  	out[3] = color2->alpha;
  	}
  else										//Interpolate color at postions between color1 and color1
  	{
  	//adjust position so that it goes from 0 to 1 in the range from color 1 & 2's position 
	position = (position-color1->position)/(color2->position - color1->position);
	
	out[0] = (color2->red   - color1->red  )*position + color1->red; 
  	out[1] = (color2->green - color1->green)*position + color1->green;
  	out[2] = (color2->blue  - color1->blue )*position + color1->blue;
  	out[3] = (color2->alpha - color1->alpha)*position + color1->alpha;
  	}
  }




//Chromatic Evaluation - 
//	This blends colors by their Hue, Saturation, and Value(Brightness) right now I just 
//	transform the RGB values stored in the CTGradientElements to HSB, in the future I may
//	streamline it to avoid transforming in and out of HSB colorspace *for later*
//
//	For the chromatic blend we shift the hue of color1 to meet the hue of color2. To do
//	this we will add to the hue's angle (if we subtract we'll be doing the inverse
//	chromatic...scroll down more for that). All we need to do is keep adding to the hue
//  until we wrap around the colorwheel and get to color2.
void chromaticEvaluation(void *info, const float *in, float *out)
  {
  float position = *in;
  
  if(*(CTGradientElement **)info == nil)	//if elementList is empty return clear color
	{
	out[0] = out[1] = out[2] = out[3] = 1;
	return;
	}
  
  //This grabs the first two colors in the sequence
  CTGradientElement *color1 = *(CTGradientElement **)info;
  CTGradientElement *color2 = color1->nextElement;
  
  float c1[4];
  float c2[4];
    
  //make sure first color and second color are on other sides of position
  while(color2 != nil && color2->position < position)
  	{
  	color1 = color2;
  	color2 = color1->nextElement;
  	}
  //if we don't have another color then make next color the same color
  if(color2 == nil)
    {
	color2 = color1;
    }
  
  
  c1[0] = color1->red; 
  c1[1] = color1->green;
  c1[2] = color1->blue;
  c1[3] = color1->alpha;
  
  c2[0] = color2->red; 
  c2[1] = color2->green;
  c2[2] = color2->blue;
  c2[3] = color2->alpha;
  
  transformRGB_HSV(c1);
  transformRGB_HSV(c2);
  resolveHSV(c1,c2);
  
  if(c1[0] > c2[0]) //if color1's hue is higher than color2's hue then 
	 c2[0] += 360;	//	we need to move c2 one revolution around the wheel
  
  
  if(position <= color1->position)			//Make all below color color1's position equal to color1
  	{
  	out[0] = c1[0]; 
  	out[1] = c1[1];
  	out[2] = c1[2];
  	out[3] = c1[3];
  	}
  else if (position >= color2->position)	//Make all above color color2's position equal to color2
  	{
  	out[0] = c2[0]; 
  	out[1] = c2[1];
  	out[2] = c2[2];
  	out[3] = c2[3];
  	}
  else										//Interpolate color at postions between color1 and color1
  	{
  	//adjust position so that it goes from 0 to 1 in the range from color 1 & 2's position 
	position = (position-color1->position)/(color2->position - color1->position);
	
	out[0] = (c2[0] - c1[0])*position + c1[0]; 
  	out[1] = (c2[1] - c1[1])*position + c1[1];
  	out[2] = (c2[2] - c1[2])*position + c1[2];
  	out[3] = (c2[3] - c1[3])*position + c1[3];
  	}
    
  transformHSV_RGB(out);
  }



//Inverse Chromatic Evaluation - 
//	Inverse Chromatic is about the same story as Chromatic Blend, but here the Hue
//	is strictly decreasing, that is we need to get from color1 to color2 by decreasing
//	the 'angle' (i.e. 90¼ -> 180¼ would be done by subtracting 270¼ and getting -180¼...
//	which is equivalent to 180¼ mod 360¼
void inverseChromaticEvaluation(void *info, const float *in, float *out)
  {
    float position = *in;
  
  if(*(CTGradientElement **)info == nil)	//if elementList is empty return clear color
	{
	out[0] = out[1] = out[2] = out[3] = 1;
	return;
	}
  
  //This grabs the first two colors in the sequence
  CTGradientElement *color1 = *(CTGradientElement **)info;
  CTGradientElement *color2 = color1->nextElement;
  
  float c1[4];
  float c2[4];
      
  //make sure first color and second color are on other sides of position
  while(color2 != nil && color2->position < position)
  	{
  	color1 = color2;
  	color2 = color1->nextElement;
  	}
  //if we don't have another color then make next color the same color
  if(color2 == nil)
    {
	color2 = color1;
    }

  c1[0] = color1->red; 
  c1[1] = color1->green;
  c1[2] = color1->blue;
  c1[3] = color1->alpha;
  
  c2[0] = color2->red; 
  c2[1] = color2->green;
  c2[2] = color2->blue;
  c2[3] = color2->alpha;

  transformRGB_HSV(c1);
  transformRGB_HSV(c2);
  resolveHSV(c1,c2);

  if(c1[0] < c2[0]) //if color1's hue is higher than color2's hue then 
	 c1[0] += 360;	//	we need to move c2 one revolution back on the wheel

  
  if(position <= color1->position)			//Make all below color color1's position equal to color1
  	{
  	out[0] = c1[0]; 
  	out[1] = c1[1];
  	out[2] = c1[2];
  	out[3] = c1[3];
  	}
  else if (position >= color2->position)	//Make all above color color2's position equal to color2
  	{
  	out[0] = c2[0]; 
  	out[1] = c2[1];
  	out[2] = c2[2];
  	out[3] = c2[3];
  	}
  else										//Interpolate color at postions between color1 and color1
  	{
  	//adjust position so that it goes from 0 to 1 in the range from color 1 & 2's position 
	position = (position-color1->position)/(color2->position - color1->position);
	
	out[0] = (c2[0] - c1[0])*position + c1[0]; 
  	out[1] = (c2[1] - c1[1])*position + c1[1];
  	out[2] = (c2[2] - c1[2])*position + c1[2];
  	out[3] = (c2[3] - c1[3])*position + c1[3];
  	}
    
  transformHSV_RGB(out);
  }



void transformRGB_HSV(float *components) //H,S,B -> R,G,B
	{
	float H, S, V;
	float R = components[0],
		  G = components[1],
		  B = components[2];
	
	float MAX = R > G ? (R > B ? R : B) : (G > B ? G : B),
	      MIN = R < G ? (R < B ? R : B) : (G < B ? G : B);
	
	if(MAX == MIN)
		H = NAN;
	else if(MAX == R)
		if(G >= B)
			H = 60*(G-B)/(MAX-MIN)+0;
		else
			H = 60*(G-B)/(MAX-MIN)+360;
	else if(MAX == G)
		H = 60*(B-R)/(MAX-MIN)+120;
	else if(MAX == B)
		H = 60*(R-G)/(MAX-MIN)+240;
	
	S = MAX == 0 ? 0 : 1 - MIN/MAX;
	V = MAX;
	
	components[0] = H;
	components[1] = S;
	components[2] = V;
	}

void transformHSV_RGB(float *components) //H,S,B -> R,G,B
	{
	float R, G, B;
	float H = fmodf(components[0],359),	//map to [0,360)
		  S = components[1],
		  V = components[2];
	
	int   Hi = (int)floorf(H/60.) % 6;
	float f  = H/60-Hi,
		  p  = V*(1-S),
		  q  = V*(1-f*S),
		  t  = V*(1-(1-f)*S);
	
	switch (Hi)
		{
		case 0:	R=V;G=t;B=p;	break;
		case 1:	R=q;G=V;B=p;	break;
		case 2:	R=p;G=V;B=t;	break;
		case 3:	R=p;G=q;B=V;	break;
		case 4:	R=t;G=p;B=V;	break;
		case 5:	R=V;G=p;B=q;	break;
		}
	
	components[0] = R;
	components[1] = G;
	components[2] = B;
	}

void resolveHSV(float *color1, float *color2)	//H value may be undefined (i.e. graycale color)
	{											//	we want to fill it with a sensible value
	if(isnan(color1[0]) && isnan(color2[0]))
		color1[0] = color2[0] = 0;
	else if(isnan(color1[0]))
		color1[0] = color2[0];
	else if(isnan(color2[0]))
		color2[0] = color1[0];
	}

@end
