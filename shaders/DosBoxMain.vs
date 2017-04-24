#version 100

precision mediump float;

attribute vec2 position;
uniform vec2 uvRatio;
// The code sets the 'aspectFix' value based on the size
// of the window/resolution of the fullscreen video mode
//
uniform float aspectFix;

varying vec2 tcNorm;

// You can always use a hard-coded value instead of the
// aspect fix from the code. Divide (4/3) by your own width/height
// ratio. For example:
//
// 4/3  Div  16/9   =
// 12/9 Div  16/9   =
// 12   Div  16
//
#define ASPECT_FIX		(12.0 / 16.0)

void main()
{
    gl_Position = vec4(position * vec2(aspectFix, 1.0), 0.0, 1.0);

	// Convert position coordinates (-1,-1)-(1,1) into
	// normalized coordinates (0,0)-(1,1) for the pixel
	// shader
	//
	tcNorm = (position + vec2(1.0)) * vec2(0.5);
}
