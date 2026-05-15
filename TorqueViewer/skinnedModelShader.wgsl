struct CommonUniforms {
    projMat: mat4x4<f32>,
    viewMat: mat4x4<f32>,
    modelMat: mat4x4<f32>,
    params1: vec4<f32>,
    params2: vec4<f32>,
    lightPos: vec4<f32>,
    lightColor: vec4<f32>,
    squareTexCoords: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> commonUniforms: CommonUniforms;

@group(1) @binding(0)
var diffuseTex: texture_2d<f32>;

@group(1) @binding(1)
var diffuseSampler: sampler;

@group(2) @binding(0)
var transformTex: texture_2d<f32>;

struct VertexInput {
    @location(0) aPosition: vec3<f32>,
    @location(1) aNormal: vec3<f32>,
    @location(2) aTexCoord0: vec2<f32>,
    @location(3) aBoneIndex0: vec4<u32>,
    @location(4) aBoneIndex1: vec4<u32>,
    @location(5) aBoneWeight0: vec4<f32>,
    @location(6) aBoneWeight1: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) vTexCoord0: vec2<f32>,
    @location(1) vLighting: vec3<f32>,
};

struct FragmentOutput {
    @location(0) Color: vec4<f32>,
};

fn getTransformTexelCoord(texelIndex: u32) -> vec2<i32> {
    let texWidth = max(u32(commonUniforms.params1.y), 1u);
    return vec2<i32>(i32(texelIndex % texWidth), i32(texelIndex / texWidth));
}

fn loadTransformRow(transformIndex: u32, rowIndex: u32) -> vec4<f32> {
    let texelIndex = transformIndex * 3u + rowIndex;
    return textureLoad(transformTex, getTransformTexelCoord(texelIndex), 0);
}

fn transformPoint(transformIndex: u32, position: vec3<f32>) -> vec3<f32> {
    let p = vec4<f32>(position, 1.0);
    let row0 = loadTransformRow(transformIndex, 0u);
    let row1 = loadTransformRow(transformIndex, 1u);
    let row2 = loadTransformRow(transformIndex, 2u);
    return vec3<f32>(dot(row0, p), dot(row1, p), dot(row2, p));
}

fn transformNormal(transformIndex: u32, normal: vec3<f32>) -> vec3<f32> {
    let row0 = loadTransformRow(transformIndex, 0u).xyz;
    let row1 = loadTransformRow(transformIndex, 1u).xyz;
    let row2 = loadTransformRow(transformIndex, 2u).xyz;
    return vec3<f32>(dot(row0, normal), dot(row1, normal), dot(row2, normal));
}

fn applySkinBlock(
    baseTransformIndex: u32,
    position: vec3<f32>,
    normal: vec3<f32>,
    boneIndices: vec4<u32>,
    boneWeights: vec4<f32>,
    posOut: ptr<function, vec3<f32>>,
    normalOut: ptr<function, vec3<f32>>
) {
    if (boneWeights.x > 0.0) {
        *posOut += transformPoint(baseTransformIndex + boneIndices.x, position) * boneWeights.x;
        *normalOut += transformNormal(baseTransformIndex + boneIndices.x, normal) * boneWeights.x;
    }
    if (boneWeights.y > 0.0) {
        *posOut += transformPoint(baseTransformIndex + boneIndices.y, position) * boneWeights.y;
        *normalOut += transformNormal(baseTransformIndex + boneIndices.y, normal) * boneWeights.y;
    }
    if (boneWeights.z > 0.0) {
        *posOut += transformPoint(baseTransformIndex + boneIndices.z, position) * boneWeights.z;
        *normalOut += transformNormal(baseTransformIndex + boneIndices.z, normal) * boneWeights.z;
    }
    if (boneWeights.w > 0.0) {
        *posOut += transformPoint(baseTransformIndex + boneIndices.w, position) * boneWeights.w;
        *normalOut += transformNormal(baseTransformIndex + boneIndices.w, normal) * boneWeights.w;
    }
}

@vertex
fn mainVert(input: VertexInput) -> VertexOutput {
    let transformOffset = u32(commonUniforms.params1.x);
    var skinnedPos = vec3<f32>(0.0);
    var skinnedNormal = vec3<f32>(0.0);

    applySkinBlock(transformOffset, input.aPosition, input.aNormal, input.aBoneIndex0, input.aBoneWeight0, &skinnedPos, &skinnedNormal);
    applySkinBlock(transformOffset, input.aPosition, input.aNormal, input.aBoneIndex1, input.aBoneWeight1, &skinnedPos, &skinnedNormal);

    // DEBUG
    //skinnedPos = input.aPosition.xyz;
    //skinnedNormal = input.aNormal.xyz;
    // END DEBUG

    let worldPos = commonUniforms.modelMat * vec4<f32>(skinnedPos, 1.0);
    let worldNormal = normalize((commonUniforms.modelMat * vec4<f32>(normalize(skinnedNormal), 0.0)).xyz);
    let lightDir = normalize(commonUniforms.lightPos.xyz - worldPos.xyz);
    let ndotl = max(dot(worldNormal, lightDir), 0.0);

    var output: VertexOutput;
    output.position = commonUniforms.projMat * commonUniforms.viewMat * worldPos;
    output.vTexCoord0 = input.aTexCoord0;
    output.vLighting = vec3<f32>(0.2) + (commonUniforms.lightColor.rgb * ndotl);
    return output;
}

@fragment
fn mainFrag(input: VertexOutput) -> FragmentOutput {
    let texColor = textureSample(diffuseTex, diffuseSampler, input.vTexCoord0);

    //if (texColor.a < commonUniforms.params2.x) {
    //    discard;
    //}

    var out: FragmentOutput;
    out.Color = vec4<f32>(texColor.rgba);//vec4<f32>(texColor.rgb /* input.vLighting*/, texColor.a);
    return out;
}
