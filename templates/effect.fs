/*{
  "DESCRIPTION": "Palette Effect starter. Image in, image out. Chromatic refraction over a soft ripple with a one-slider mix.",
  "CREDIT": "Palette",
  "ISFVSN": "2",
  "CATEGORIES": ["Effect", "Audio Reactive"],
  "INPUTS": [
    { "NAME": "inputImage", "TYPE": "image" },
    { "NAME": "amount", "LABEL": "Amount", "TYPE": "float", "GROUP": "Look", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0 },
    { "NAME": "rippleScale", "LABEL": "Ripple Scale", "TYPE": "float", "GROUP": "Look", "DEFAULT": 14.0, "MIN": 2.0, "MAX": 40.0 },
    { "NAME": "split", "LABEL": "Chroma Split", "TYPE": "float", "GROUP": "Look", "DEFAULT": 0.35, "MIN": 0.0, "MAX": 1.0 },
    { "NAME": "tint", "LABEL": "Tint", "TYPE": "color", "GROUP": "Look", "DEFAULT": [1.0, 1.0, 1.0, 1.0] },
    { "NAME": "center", "LABEL": "Center", "TYPE": "point2D", "GROUP": "Look", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0] },
    { "NAME": "audioReact", "LABEL": "Audio React", "TYPE": "float", "GROUP": "Audio Reactivity", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0 }
  ],
  "PALETTE": { "kind": "effect", "version": 1 }
}*/

// Palette rules: no #version here, no texture2D, no gl_FragColor alias games.
// The host prepends the dialect prelude (330 core on desktop, 300 es on phones).

void main() {
    vec2 uv = isf_FragNormCoord;
    vec2 p = uv - center;
    p.x *= RENDERSIZE.x / RENDERSIZE.y;
    float d = length(p);

    // Soft ripple, bass makes it breathe.
    float drive = 1.0 + audioBass * audioReact * 1.5;
    float w = sin(d * rippleScale * drive - TIME * 2.0) * 0.5 + 0.5;
    w *= smoothstep(1.2, 0.0, d);
    vec2 dir = d > 1e-4 ? p / d : vec2(0.0);
    vec2 offs = dir * w * 0.04 * amount;

    // Chromatic split along the ripple direction.
    float s = split * 0.012 * amount;
    float r = IMG_NORM_PIXEL(inputImage, uv + offs + dir * s).r;
    float g = IMG_NORM_PIXEL(inputImage, uv + offs).g;
    float b = IMG_NORM_PIXEL(inputImage, uv + offs - dir * s).b;
    vec3 col = vec3(r, g, b) * tint.rgb;

    // Specular lift on the wave crest.
    col += vec3(0.08) * w * amount;

    gl_FragColor = vec4(col, 1.0);
}
