#ifdef GL_ES
precision highp float;
#endif

varying mediump vec2 coordinate; 
uniform lowp sampler2D videoframe;
uniform lowp vec2 screenSize;

/*
void main()
{
    vec4 color = texture2D(videoframe, coordinate);
    //float odd = mod(coordinate.y * screenSize.y, 2.0);
    //gl_FragColor = color * vec4(odd, odd, odd, odd);
    gl_FragColor = color;
}
*/

lowp vec3 weight(float pixel)
{
    pixel = mod(pixel + 3.0, 3.0);
    if (pixel >= 2.0) // Blue
        return lowp vec3(pixel - 2.0, 0.0, 3.0 - pixel);
    else if (pixel >= 1.0) // Green
        return lowp vec3(0.0, 2.0 - pixel, pixel - 1.0);
    else // Red
        return lowp vec3(1.0 - pixel, pixel, 0.0);
}

void main()
{
    lowp float y = mod(coordinate.y * screenSize.y/2.0, 1.0);
    lowp float intensity = exp(-0.2 * y);

    vec2 one_x = vec2(1.0 / (3.0 * screenSize.x/2.0), 0.0);

    lowp vec3 color = texture2D(videoframe, coordinate.xy - 0.0 * one_x).rgb;
    lowp vec3 color_prev = texture2D(videoframe, coordinate.xy - 1.0 * one_x).rgb;
    lowp vec3 color_prev_prev = texture2D(videoframe, coordinate.xy - 2.0 * one_x).rgb;

    float pixel_x = 3.0 * coordinate.x * screenSize.x/2.0;

    lowp vec3 weight = weight(pixel_x - 0.0);
    lowp vec3 weight_prev = weight(pixel_x - 1.0);
    lowp vec3 weight_prev_prev = weight(pixel_x - 2.0);

    vec3 result =
        0.8 * color * weight +
        0.6 * color_prev * weight_prev +
        0.3 * color_prev_prev * weight_prev_prev;

    result = 2.3 * pow(result, vec3(1.4));

    gl_FragColor = lowp vec4(intensity * result, 1.0);
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

const float gamma = 2.4;
const float shine = 0.05;
const float blend = 0.65;

float dist(vec2 coord, vec2 source)
{
    vec2 delta = coord - source;
    return sqrt(dot(delta, delta));
}

float color_bloom(vec3 color)
{
    const vec3 gray_coeff = vec3(0.30, 0.59, 0.11);
    float bright = dot(color, gray_coeff);
    return mix(1.0 + shine, 1.0 - shine, bright);
}

vec3 lookup(float offset_x, float offset_y, vec2 coord)
{
    vec2 offset = vec2(offset_x, offset_y);
    vec3 color = texture2D(videoframe, coord).rgb;
    float delta = dist(fract(pixel_no), offset + vec2(0.5));
    return color * exp(-gamma * delta * color_bloom(color));
}

void main()
{
    vec3 mid_color = lookup(0.0, 0.0, c11);
    vec3 color = vec3(0.0);
    color += lookup(-1.0, -1.0, c00);
    color += lookup( 0.0, -1.0, c10);
    color += lookup( 1.0, -1.0, c20);
    color += lookup(-1.0,  0.0, c01);
    color += mid_color;
    color += lookup( 1.0,  0.0, c21);
    color += lookup(-1.0,  1.0, c02);
    color += lookup( 0.0,  1.0, c12);
    color += lookup( 1.0,  1.0, c22);
    gl_FragColor = vec4(mix(1.2 * mid_color, color, blend), 1.0);

    //gl_FragColor = vec4(out_color, 1.0);
}
*/