#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;

// Video fill-mode crop ('V' key): samples a centered sub-rectangle of the
// texture instead of the whole thing, so the *content* (excluding mpv's
// own letterbox/pillarbox bars) is what stretches to fill the screen.
// Identity (scale=1, offset=0) reproduces plain full-texture sampling.
uniform vec2 uUvScale;
uniform vec2 uUvOffset;

void main() {
    FragColor = texture(uTexture, vUV * uUvScale + uUvOffset);
}
