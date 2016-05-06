#version 120

attribute vec2 position;
uniform vec2 uvRatio;

// varying vec2 texcoord;
varying vec2 tcNorm;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);

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
