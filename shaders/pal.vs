#version 120

attribute vec2 position;
uniform vec2 uvRatio;
// The code sets the 'aspectFix' value based on the size
// of the window/resolution of the fullscreen video mode
//
uniform float aspectFix;

// varying vec2 texcoord;
varying vec2 tcNorm;

// You can always use a hard-coded value instead of the
// aspect fix from the code
//
#define ASPECT_FIX		(12.0 / 16.0)

void main()
{
    gl_Position = vec4(position * vec2(aspectFix, 1.0), 0.0, 1.0);

	// Convert position coordinates (-1,-1)-(1,1) into
	// normalized coordinates (0,0)-(1,1)
	//
	tcNorm = (position + vec2(1.0)) * vec2(0.5);

	// Convert normalized coordinates into UV texture
	// coordinates
	//
//	texcoord.x = tcNorm.x * uvRatio.x;
//	texcoord.y = (1.0 - uvRatio.y) + (tcNorm.y * uvRatio.y);
}
