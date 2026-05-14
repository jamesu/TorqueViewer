struct CommonUniforms {
    projMat: mat4x4<f32>,
    viewMat: mat4x4<f32>,
    modelMat: mat4x4<f32>,
    params1: vec4<f32>, // ndcPixelScale.xy, lineWidth
    params2: vec4<f32>, // alphaTestF
    lightPos: vec4<f32>,
    lightColor: vec4<f32>,
};

@group(0) @binding(0) var<uniform> commonUniforms: CommonUniforms;

struct VertexInput {
    @location(0) aPosition: vec3<f32>,
    @location(1) aNext: vec3<f32>,
    @location(2) aNormal: vec3<f32>,
    @location(3) aColor0: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) vColor0: vec4<f32>,
};

struct FragmentOutput {
    @location(0) Color: vec4<f32>,
};

@vertex
fn mainVert(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let mvpMat: mat4x4<f32> = commonUniforms.projMat * commonUniforms.viewMat;
    let startClip = mvpMat * vec4<f32>(input.aPosition, 1.0);
    let endClip = mvpMat * vec4<f32>(input.aNext, 1.0);

    if (startClip.w <= 0.0 || endClip.w <= 0.0) {
        output.position = vec4<f32>(0.0, 0.0, 2.0, 1.0);
        output.vColor0 = input.aColor0;
        return output;
    }

    let startNdc = startClip.xy / startClip.w;
    let endNdc = endClip.xy / endClip.w;
    let lineDir = endNdc - startNdc;
    let dirLen = length(lineDir);
    var perp = vec2<f32>(0.0, 1.0);
    if (dirLen > 0.000001) {
        let dir = lineDir / dirLen;
        perp = vec2<f32>(-dir.y, dir.x);
    }

    let offsetNdc = perp * input.aNormal.x * commonUniforms.params1.z * commonUniforms.params1.xy;
    var clipSpace = startClip;
    clipSpace.x = clipSpace.x + offsetNdc.x * startClip.w;
    clipSpace.y = clipSpace.y + offsetNdc.y * startClip.w;

    output.position = clipSpace;
    output.vColor0 = input.aColor0;
    return output;
}

@fragment
fn mainFrag(input: VertexOutput) -> FragmentOutput {
    var out: FragmentOutput;
    out.Color = input.vColor0;
    return out;
}
