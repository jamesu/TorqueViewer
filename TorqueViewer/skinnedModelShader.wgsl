// Skinned instance model shader

struct InstanceInfo {
    // root transform offset
    // ??
    // ??
    // ??
    instanceConfig: vec4<i32>,

    modelMat: mat3x4<f32>,
    invModel: mat3x4<f32> 
};

struct CommonUniforms {
    projMat: mat4x4<f32>,
    viewMat: mat4x4<f32>,
    modelMat: mat4x4<f32>,
    params1: vec4<f32>, // viewportScale.xy, lineWidth
    params2: vec4<f32>, // alphaTestF
    lightPos: vec4<f32>,
    lightColor: vec4<f32>,

    // transformsPerRow, 
    // instanceLookupPerRow
    globalInstanceConfig: vec4<i32>,

    instances: array<InstanceInfo, 10>
};

struct LightUnit {
   diffuse: vec4<f32>,
   ambient: vec4<f32>,
   specular: vec4<f32>,
   
   position: vec4<f32>,
   spotDirectionCutoff: vec4<f32>, // always 180
   attenEnabled: vec4<f32> // const, lin, quad, enables
};

// Uniforms

@group(0) @binding(0) var<uniform> commonUniforms: CommonUniforms;

// Material

@group(1) @binding(0)
var texs0: texture_2d<f32>; // diffuse
@group(1) @binding(1)
var texs1: texture_2d<f32>; // emap alpha
@group(1) @binding(2)
var texs2: texture_2d<f32>; // dmap
@group(1) @binding(3)
var texs3: texture_2d<f32>; // emap
@group(1) @binding(4)
var samplers0: sampler;
@group(1) @binding(5)
var samplers1: sampler;
@group(1) @binding(6)
var samplers2: sampler;
@group(1) @binding(7)
var samplers3: sampler;

// Transforms

@group(2) @binding(0) var transformTex: texture_2d<f32>; // transformed skinned mesh (NOT global shape nodes)

struct VertexInput {
    @location(0) aPosition: vec3<f32>,
    @location(1) aNormal: vec3<f32>,
    @location(2) aTexCoord0: vec2<f32>,
    @location(3) aBoneIndex0: vec4<u32>,   // influences 0..3
    @location(4) aBoneWeight0: vec4<f32>,
    @location(5) aBoneIndex1: vec4<u32>,   // influences 4..7
    @location(6) aBoneWeight1: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) vTexCoord0: vec2<f32>,
    @location(1) vColor0: vec4<f32>,
    @location(2) vNormal0: vec4<f32>,
};

struct FragmentOutput {
    @location(0) Color: vec4<f32>,
};

fn expandToMat4(m: mat3x4<f32>) -> mat4x4<f32> {
    return mat4x4<f32>(
        vec4<f32>(m[0].xyz, 0.0),                     // column 0
        vec4<f32>(m[1].xyz, 0.0),                     // column 1
        vec4<f32>(m[2].xyz, 0.0),                     // column 2
        vec4<f32>(m[0].w, m[1].w, m[2].w, 1.0)        // column 3 (homogeneous row)
    );
}

fn texelCoord1D(index: u32, texWidth: u32) -> vec2<i32> {
    let x = i32(index % texWidth);
    let y = i32(index / texWidth);
    return vec2<i32>(x, y);
}

fn loadAffine3x4(
    matrixIndex: u32,
    texWidth: u32
) -> mat3x4<f32> {
    let baseTexel = matrixIndex * 3u;

    let row0 = textureLoad(transformTex, texelCoord1D(baseTexel + 0u, texWidth), 0);
    let row1 = textureLoad(transformTex, texelCoord1D(baseTexel + 1u, texWidth), 0);
    let row2 = textureLoad(transformTex, texelCoord1D(baseTexel + 2u, texWidth), 0);

    return mat3x4<f32>(row0, row1, row2);
}

@vertex
fn mainVert(input: VertexInput,
            @builtin(instance_index) instanceId: u32
    ) -> VertexOutput {
    let globalTransformWidth = commonUniforms.globalInstanceConfig.x;
    let globalLookupWidth = commonUniforms.globalInstanceConfig.y;
    let inst = commonUniforms.instances[instanceId];

    let normal: vec3<f32> = normalize((expandToMat4(inst.modelMat) * vec4<f32>(input.aNormal, 0.0)).xyz);
    let instanceBase = inst.instanceConfig.x;

    var skinnedPos = vec3<f32>(0.0);
    var skinnedNrm = vec3<f32>(0.0);

    // First set
    for (var k: u32 = 0u; k < 4u; k = k + 1u)
    {
        let w = input.aBoneWeight0[k];
        if (w > 0.0) {
            let localBone = input.aBoneIndex0[k];
            let m = loadAffine3x4(instanceBase + localBone, globalTransformWidth);

            skinnedPos += transformPoint(m, pos) * w;
            skinnedNrm += transformVector(m, nrm) * w;
        }
    }

    // Second set
    for (var k: u32 = 0u; k < 4u; k = k + 1u)
    {
        let w = input.aBoneWeight1[k];
        if (w > 0.0) {
            let localBone = input.aBoneIndex1[k];
            let m = loadAffine3x4(instanceBase + localBone, globalTransformWidth);

            skinnedPos += transformPoint(m, pos) * w;
            skinnedNrm += transformVector(m, nrm) * w;
        }
    }

    let nrm = normalize((expandToMat4(inst.modelMat) * vec4<f32>(skinnedNrm, 0.0)).xyz);
    let lightDir: vec3<f32> = normalize(commonUniforms.lightPos.xyz);
    let NdotL: f32 = max(dot(normal, lightDir), 0.0);
    let diffuse = vec4<f32>(commonUniforms.lightColor.xyz, 1.0);

    let mvpMat: mat4x4<f32> = commonUniforms.projMat * commonUniforms.viewMat * expandToMat4(inst.modelMat);

    var output: VertexOutput;
    output.position = mvpMat * vec4<f32>(input.aPosition, 1.0);
    output.vTexCoord0 = input.aTexCoord0;
    output.vColor0 = vec4<f32>(1.0, 1.0, 1.0, 1.0); // Set to white color as per original shader
    output.vColor0.a = 1.0;
    output.vNormal0 = vec4<f32>(nrm.xyz, 1.0);

    return output;
}

@fragment
fn mainFrag(input: VertexOutput) -> FragmentOutput {
    var color: vec4<f32> = textureSample(texs0, samplers0, input.vTexCoord0);

    if (color.a > commonUniforms.params2.x) {
        discard;
    }

    var outputColor: vec4<f32>;
    outputColor.r = color.r * input.vColor0.r * input.vColor0.a;
    outputColor.g = color.g * input.vColor0.g * input.vColor0.a;
    outputColor.b = color.b * input.vColor0.b * input.vColor0.a;
    outputColor.a = color.a;

    var out: FragmentOutput;
    out.Color = outputColor;
    return out;
}
