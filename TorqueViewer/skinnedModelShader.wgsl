struct CommonUniforms {
    projMat: mat4x4<f32>,
    viewMat: mat4x4<f32>,
    modelMat: mat4x4<f32>,
    params1: vec4<f32>, // viewportScale.xy, lineWidth
    params2: vec4<f32>, // alphaTestF
    lightPos: vec4<f32>,
    lightColor: vec4<f32>,
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

@group(2) @binding(0) var transformTex: texture_2d<f32>; // shape nodes
@group(2) @binding(1) var transformLookupTex: texture_2d<u32>; // mesh -> shape lookup

struct VertexInput {
    @location(0) aPosition: vec3<f32>,
    @location(1) aNormal: vec3<f32>,
    @location(2) aTexCoord0: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) vTexCoord0: vec2<f32>,
    @location(1) vColor0: vec4<f32>,
};

struct FragmentOutput {
    @location(0) Color: vec4<f32>,
};


@vertex
fn mainVert(input: VertexInput) -> VertexOutput {
    let normal: vec3<f32> = normalize((commonUniforms.modelMat * vec4<f32>(input.aNormal, 0.0)).xyz);
    let lightDir: vec3<f32> = normalize(commonUniforms.lightPos.xyz);
    let NdotL: f32 = max(dot(normal, lightDir), 0.0);
    let diffuse = vec4<f32>(commonUniforms.lightColor.xyz, 1.0);

    let mvpMat: mat4x4<f32> = commonUniforms.projMat * commonUniforms.viewMat * commonUniforms.modelMat;

    var output: VertexOutput;
    output.position = mvpMat * vec4<f32>(input.aPosition, 1.0);
    output.vTexCoord0 = input.aTexCoord0;
    output.vColor0 = vec4<f32>(1.0, 1.0, 1.0, 1.0); // Set to white color as per original shader
    output.vColor0.a = 1.0;

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
