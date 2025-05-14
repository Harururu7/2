#include <glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <fstream>
#include <cmath>

// Шейдеры
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
out vec2 uv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    uv = aPos * 0.5 + 0.5;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform vec3 cameraPos;
uniform vec3 cameraFront;
uniform vec3 cameraUp;
uniform float aspectRatio;

// Константы
const vec3 skyColor = vec3(0.4, 0.7, 1.0);
const vec3 sunColor = vec3(1.0, 0.9, 0.7);
const vec3 sunDir = normalize(vec3(0.2, 0.5, -1.0));
const float sunIntensity = 1.5;
const float SHADOW_BIAS = 0.001;
const float RAY_BIAS = 0.0001;
const int MAX_RECURSION_DEPTH = 1;
const float MIN_RAY_DISTANCE = 0.001;
const float FOG_START = 5.0;
const float FOG_DENSITY = 0.05;

struct Material {
    vec3 albedo; //color
    float roughness; //шероховатость
    float metallic; // метал, при 1 работает как отражение 
    float specular; // отражение 
    float subsurface; // блик
    float clearCoat; // глянец
};

struct Sphere {
    vec3 center;
    float radius;
    Material material;
};

Sphere spheres[5] = Sphere[](
    Sphere(vec3(3.0, 0.0, -1.0), 0.5, Material(vec3(1.0, 0.3, 0.3), 1.0, 0.0, 0.0, 0.034, 0.0)),  // Красная матовая
    Sphere(vec3(3.0, 0.0, -2.0), 0.5, Material(vec3(0.0, 0.0, 0.95), 0.95, 0.0, 0.5, 0.034, 0.2)), // Пластиковая
    Sphere(vec3(-1.0, 0.0, -1.0), 0.5, Material(vec3(0.3, 1.0, 0.3), 0.7, 0.0, 0.0, 0.034, 0.0)),   // Зеленая матовая
    Sphere(vec3(1.5, 0.4, -3.0), 0.6, Material(vec3(0.5, 0.5, 0.5), 0.1, 0.35, 0.0, 0.034, 0.0)),    // металлическая сфера
    Sphere(vec3(0.0, -100.5, -1.0), 100.0, Material(vec3(0.9, 0.9, 0.3), 0.7, 0.0, 0.1, 0.1, 0.0)) // Пол тоже сфера
);

// Функция случайного числа
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123);
}

// Функция пересечения сферы
bool sphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius, out float t) {
    vec3 oc = ro - center;
    float a = dot(rd, rd);
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - radius*radius;
    float d = b*b - 4.0*a*c;
    
    if(d < 0.0) return false;
    
    t = (-b - sqrt(d))/(2.0*a);
    if(t > MIN_RAY_DISTANCE) return true;
    
    t = (-b + sqrt(d))/(2.0*a);
    return t > MIN_RAY_DISTANCE;
}

// Функция неба
vec3 getSkyColor(vec3 rd) {
    float sunDot = max(0.0, dot(rd, sunDir));
    float horizon = 1.0 - abs(rd.y);
    
    vec3 color = mix(vec3(0.5, 0.7, 1.0), vec3(0.1, 0.2, 0.4), horizon*horizon);
    color += sunColor * pow(sunDot, 256.0) * sunIntensity;
    color += sunColor * pow(sunDot, 16.0) * 0.6;
    
    return color;
}

// Функция теней
float calculateShadow(vec3 point, vec3 normal, vec3 lightDir) {
    float shadow = 1.0;
    float t;
    float lightRadius = 0.05;
    float shadowSoftness = 0.5;
    float contactShadow = 1.0;
    
    vec3 shadowRayOrigin = point + normal * max(SHADOW_BIAS, length(point)*0.0001);
    
    for(int i = 0; i < 5; i++) {
        if(sphereIntersect(shadowRayOrigin, lightDir, spheres[i].center, spheres[i].radius, t)) {
            float penumbra = 1.0 - smoothstep(0.0, lightRadius, t);
            shadow = min(shadow, mix(1.0, penumbra, shadowSoftness));
            float contact = 1.0 - smoothstep(0.0, 0.1, t) * 0.8;
            contactShadow = min(contactShadow, contact);
        }
    }
    
    float ao = 1.0;
    float aoRadius = 0.2;
    int aoSamples = 4;
    for(int i = 0; i < aoSamples; i++) {
        vec3 randomDir = normalize(vec3(
            sin(i*123.456), 
            cos(i*456.789), 
            sin(i*789.123)*cos(i*321.654)
        ));
        randomDir = normalize(randomDir + normal);
        
        float t;
        if(sphereIntersect(point + normal*RAY_BIAS, randomDir, spheres[i%4].center, spheres[i%4].radius, t) && t < aoRadius) {
            ao -= (aoRadius - t)/aoRadius * (1.0/float(aoSamples));
        }
    }
    ao = clamp(ao, 0.3, 1.0);
    
    shadow = mix(shadow, 1.0, 0.3);
    shadow = min(shadow, contactShadow);
    shadow *= ao;
    
    return clamp(shadow, 0.3, 1.0);
}

// Функция освещения
vec3 calculateLighting(vec3 pos, vec3 normal, vec3 viewDir, Material mat) {
    float diff = max(0.0, dot(normal, sunDir));
    float shadow = calculateShadow(pos, normal, sunDir);
    
    vec3 result = mat.albedo * diff * shadow * sunIntensity;
    
    if(mat.subsurface > 0.0) {
        float backLight = max(0.0, dot(-sunDir, normal)) * 0.5;
        result += mat.albedo * backLight * mat.subsurface * sunIntensity;
    }
    
    if(mat.specular > 0.0 || mat.metallic > 0.0) {
        vec3 halfVec = normalize(sunDir + viewDir);
        float specAngle = max(0.0, dot(normal, halfVec));
        float specular = mat.specular;
        specular = mix(specular, 1.0, mat.metallic);
        float spec = pow(specAngle, 32.0/(mat.roughness*mat.roughness));
        result += sunColor * spec * shadow * specular;
    }
    
    if(mat.clearCoat > 0.0) {
        vec3 reflectDir = reflect(-viewDir, normal);
        float spec = pow(max(0.0, dot(reflectDir, sunDir)), 32.0/(mat.roughness*mat.roughness*0.25));
        result += sunColor * spec * shadow * mat.clearCoat * 0.5;
    }
    
    return result;
}

// Функция отражений
vec3 calculateReflection(vec3 ro, vec3 rd, vec3 normal, vec3 point, Material mat) {
    vec3 currentColor = vec3(0.0);
    vec3 currentWeight = vec3(1.0);
    vec3 currentRayOrigin = point;
    vec3 currentRayDir = rd;
    vec3 currentNormal = normal;
    
    for (int depth = 0; depth < MAX_RECURSION_DEPTH; depth++) {
        vec3 reflectedDir = reflect(currentRayDir, currentNormal);
        float tMin = 1.0/0.0;
        int hitId = -1;
        
        for(int i = 0; i < 5; i++) {
            float t;
            if(sphereIntersect(currentRayOrigin + currentNormal*RAY_BIAS, reflectedDir, 
                             spheres[i].center, spheres[i].radius, t) && t < tMin) {
                tMin = t;
                hitId = i;
            }
        }

        if(hitId != -1) {
            vec3 pos = currentRayOrigin + reflectedDir * tMin;
            vec3 n = normalize(pos - spheres[hitId].center);
            n = dot(reflectedDir, n) < 0.0 ? n : -n;
            vec3 viewDir = -reflectedDir;
            vec3 objectColor = calculateLighting(pos, n, viewDir, spheres[hitId].material);
            
            float reflectionFactor = max(mat.metallic, mat.specular * 0.5);
            if(reflectionFactor > 0.01) {
                currentWeight *= reflectionFactor * (1.0 - mat.roughness);
                currentRayOrigin = pos;
                currentRayDir = reflectedDir;
                currentNormal = n;
                currentColor += currentWeight * objectColor;
            } else {
                currentColor += currentWeight * objectColor;
                break;
            }
        } else {
            vec3 reflectedColor = getSkyColor(reflectedDir);
            currentColor += currentWeight * reflectedColor;
            break;
        }
    }
    return currentColor;
}

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= aspectRatio;
    
    vec3 ro = cameraPos;
    vec3 rd = normalize(cameraFront + p.x * normalize(cross(cameraFront, cameraUp)) + p.y * cameraUp);
    
    vec3 color = getSkyColor(rd);
    float tMin = 1.0/0.0;
    int hitId = -1;
    
    // Поиск ближайшего пересечения (теперь проверяем 5 сфер)
    for(int i = 0; i < 5; i++) {
        float t;
        if(sphereIntersect(ro, rd, spheres[i].center, spheres[i].radius, t) && t < tMin) {
            tMin = t;
            hitId = i;
        }
    }
    
    if(hitId != -1) {
        vec3 pos = ro + rd * tMin;
        vec3 normal = normalize(pos - spheres[hitId].center);
        normal = dot(rd, normal) < 0.0 ? normal : -normal;
        vec3 viewDir = -rd;
        Material mat = spheres[hitId].material;
        vec3 objectColor = calculateLighting(pos, normal, viewDir, mat);
        
        // Усилим отражения для металлических поверхностей
        float reflectionFactor = max(mat.metallic, mat.specular * 0.5);
        if(reflectionFactor > 0.01) {
            vec3 reflectedColor = calculateReflection(ro, rd, normal, pos, mat);
            objectColor = mix(objectColor, reflectedColor, reflectionFactor * (1.0 - mat.roughness));
            
            // Для металлов полностью заменяем диффузный цвет на отражения
            if(mat.metallic > 0.5) {
                objectColor = mix(objectColor, reflectedColor, 0.9);
            }
        }
        
        // Туман
        float dist = tMin;
        float fog = (dist < FOG_START) ? 0.0 : clamp(1.0 - exp(-(dist-FOG_START) * FOG_DENSITY), 0.0, 1.0);
        color = mix(objectColor, color, fog);
    }
    
    // Тоновая коррекция
    color = color / (color + vec3(1.0));
    FragColor = vec4(pow(color, vec3(1.0/2.2)), 1.0);
}
)";

// Камера
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 0.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = -90.0f, pitch = 0.0f;
bool cameraMoved = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    static float lastX = 400, lastY = 300;
    static bool firstMouse = true;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    float cosPitch = cos(glm::radians(pitch));
    front.x = cos(glm::radians(yaw)) * cosPitch;
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cosPitch;
    cameraFront = glm::normalize(front);
    cameraMoved = true;
}

void processInput(GLFWwindow* window) {
    float cameraSpeed = 0.05f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cameraPos += cameraSpeed * cameraFront;
        cameraMoved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cameraPos -= cameraSpeed * cameraFront;
        cameraMoved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
        cameraMoved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
        cameraMoved = true;
    }
}

int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(800, 600, "Ray Tracing", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        return -1;
    }

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Шейдеры
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Проверка компиляции вершинного шейдера
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Проверка компиляции фрагментного шейдера
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
    }

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Проверка линковки программы
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
    }

    // Вершинный буфер
    float vertices[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    
    GLuint cameraPosLoc = glGetUniformLocation(shaderProgram, "cameraPos");
    GLuint cameraFrontLoc = glGetUniformLocation(shaderProgram, "cameraFront");
    GLuint cameraUpLoc = glGetUniformLocation(shaderProgram, "cameraUp");
    GLuint aspectRatioLoc = glGetUniformLocation(shaderProgram, "aspectRatio");

    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        if (cameraMoved) {
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(shaderProgram);

            
            glUniform3f(cameraPosLoc, cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform3f(cameraFrontLoc, cameraFront.x, cameraFront.y, cameraFront.z);
            glUniform3f(cameraUpLoc, cameraUp.x, cameraUp.y, cameraUp.z);

            
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            glUniform1f(aspectRatioLoc, (float)width / (float)height);

            glBindVertexArray(VAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            cameraMoved = false;
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glfwTerminate();
    return 0;
}