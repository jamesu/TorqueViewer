struct CommonUniforms {
    projMat: mat4x4<f32>,
    viewMat: mat4x4<f32>,
    modelMat: mat4x4<f32>,
    params1: vec4<f32>, // viewportScale.xy, lineWidth
    params2: vec4<f32>, // alphaTestF
    params3: vec4<f32>, // material flags / debug toggles
    lightPos: vec4<f32>,
    lightColor: vec4<f32>,
    squareTexCoords: array<vec4<f32>, 16>,
};

@group(0) @binding(0) var<uniform> commonUniforms: CommonUniforms;


@group(1) @binding(0) var texture0: texture_2d<f32>;
@group(1) @binding(1) var sampler0: sampler;

struct VertexInput {
    @location(0) aPosition: vec3<f32>,
    @location(1) aNormal: vec3<f32>,
    @location(2) aTexCoord0: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) vTexCoord0: vec2<f32>,
    @location(1) vLighting: vec3<f32>,
    @location(2) vWorldNormal: vec3<f32>,
};

struct FragmentOutput {
    @location(0) Color: vec4<f32>,
};

fn inverse3x3(m: mat3x3<f32>) -> mat3x3<f32> {
    let a = m[0];
    let b = m[1];
    let c = m[2];
    let r0 = cross(b, c);
    let r1 = cross(c, a);
    let r2 = cross(a, b);
    let invDet = 1.0 / dot(a, r0);
    return mat3x3<f32>(r0 * invDet, r1 * invDet, r2 * invDet);
}


@vertex
fn mainVert(input: VertexInput) -> VertexOutput {
    let mvpMat: mat4x4<f32> = commonUniforms.projMat * commonUniforms.viewMat * commonUniforms.modelMat;
    let worldPos = commonUniforms.modelMat * vec4<f32>(input.aPosition, 1.0);
    let model3 = mat3x3<f32>(commonUniforms.modelMat[0].xyz, commonUniforms.modelMat[1].xyz, commonUniforms.modelMat[2].xyz);
    let normalMat = transpose(inverse3x3(model3));
    let normal: vec3<f32> = normalize(normalMat * input.aNormal);
    let lightVec = commonUniforms.lightPos.xyz - worldPos.xyz;
    let lightDist = length(lightVec);
    let lightDir = select(vec3<f32>(0.0), normalize(lightVec), lightDist > 1e-5);
    let isDirectional = commonUniforms.lightPos.w > 0.5;
    let ndotlRaw = dot(normal, select(lightDir, normalize(commonUniforms.lightPos.xyz), isDirectional));
    let ndotl = select(
        max(ndotlRaw, 0.0),
        clamp(ndotlRaw * 0.5 + 0.5, 0.0, 1.0),
        isDirectional
    );
    let pointAtten = select(
        1.0,
        clamp(1.0 - (lightDist / max(commonUniforms.lightColor.a, 1.0)), 0.0, 1.0),
        !isDirectional && commonUniforms.lightColor.a > 0.0
    );

    var output: VertexOutput;
    output.position = mvpMat * vec4<f32>(input.aPosition, 1.0);
    output.vTexCoord0 = input.aTexCoord0;
    output.vLighting = vec3<f32>(0.2) + (commonUniforms.lightColor.rgb * ndotl * pointAtten);
    output.vWorldNormal = normal;

    return output;
}

@fragment
fn mainFrag(input: VertexOutput) -> FragmentOutput {
    if (commonUniforms.params2.z > 1.5) {
        var debugOut: FragmentOutput;
        debugOut.Color = vec4<f32>(normalize(input.vWorldNormal) * 0.5 + vec3<f32>(0.5), 1.0);
        return debugOut;
    }

    if (commonUniforms.params2.z > 0.5) {
        var debugOut: FragmentOutput;
        debugOut.Color = commonUniforms.squareTexCoords[2];
        return debugOut;
    }

    var color: vec4<f32> = textureSample(texture0, sampler0, input.vTexCoord0);

    if (color.a > commonUniforms.params2.x) {
        discard;
    }

    var outputColor: vec4<f32>;
    let lighting = select(input.vLighting, vec3<f32>(1.0), (commonUniforms.params3.x > 0.5) || (commonUniforms.params3.y > 0.5));
    outputColor.r = color.r * lighting.r;
    outputColor.g = color.g * lighting.g;
    outputColor.b = color.b * lighting.b;
    outputColor.a = color.a;

    var out: FragmentOutput;
    out.Color = outputColor;
    return out;
}
