#ifdef GL_ES
precision highp float;
#endif

attribute vec4 position; 
attribute mediump vec4 textureCoordinate; 
varying mediump vec2 coordinate; 
uniform lowp vec2 screenSize;

void main()
{
    gl_Position = position;
    coordinate = textureCoordinate.xy;
}


/*
varying vec2 c00;
varying vec2 c10;
varying vec2 c20;
varying vec2 c01;
varying vec2 c11;
varying vec2 c21;
varying vec2 c02;
varying vec2 c12;
varying vec2 c22;
varying vec2 pixel_no;


void main()
{
    gl_Position = position;
    float dx = 1.0 / screenSize.x;
    float dy = 1.0 / screenSize.y;

    vec2 tex = textureCoordinate.xy;
    c00 = tex + vec2(-dx, -dy);
    c10 = tex + vec2(  0, -dy);
    c20 = tex + vec2( dx, -dy);
    c01 = tex + vec2(-dx,   0);
    c11 = tex + vec2(  0,   0);
    c21 = tex + vec2( dx,   0);
    c02 = tex + vec2(-dx,  dy);
    c12 = tex + vec2(  0,  dy);
    c22 = tex + vec2( dx,  dy);
    pixel_no = tex * screenSize;
}
*/