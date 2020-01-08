﻿using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

[RequireComponent(typeof(MeshFilter), typeof(MeshRenderer))]
public class AzureKinectScreen : MonoBehaviour
{
    public Camera mainCamera;
    public MeshFilter meshFilter;
    public MeshRenderer meshRenderer;

    public void Setup(AzureKinectCalibration calibration)
    {
        print("calibration.ColorCamera.MetricRadius: " + calibration.ColorCamera.MetricRadius);
        print("calibration.ColorCamera.Intrinsics.Cx: " + calibration.ColorCamera.Intrinsics.Cx);
        print("calibration.ColorCamera.Intrinsics.Cy: " + calibration.ColorCamera.Intrinsics.Cy);
        print("calibration.ColorCamera.Intrinsics.Fx: " + calibration.ColorCamera.Intrinsics.Fx);
        print("calibration.ColorCamera.Intrinsics.Fy: " + calibration.ColorCamera.Intrinsics.Fy);
        print("calibration.ColorCamera.Intrinsics.K1: " + calibration.ColorCamera.Intrinsics.K1);
        print("calibration.ColorCamera.Intrinsics.K2: " + calibration.ColorCamera.Intrinsics.K2);
        print("calibration.ColorCamera.Intrinsics.K3: " + calibration.ColorCamera.Intrinsics.K3);
        print("calibration.ColorCamera.Intrinsics.K4: " + calibration.ColorCamera.Intrinsics.K4);
        print("calibration.ColorCamera.Intrinsics.K5: " + calibration.ColorCamera.Intrinsics.K5);
        print("calibration.ColorCamera.Intrinsics.K6: " + calibration.ColorCamera.Intrinsics.K6);
        print("calibration.ColorCamera.Intrinsics.Codx: " + calibration.ColorCamera.Intrinsics.Codx);
        print("calibration.ColorCamera.Intrinsics.Cody: " + calibration.ColorCamera.Intrinsics.Cody);
        print("calibration.ColorCamera.Intrinsics.P2: " + calibration.ColorCamera.Intrinsics.P2);
        print("calibration.ColorCamera.Intrinsics.P1: " + calibration.ColorCamera.Intrinsics.P1);

        print("calibration.DepthCamera.MetricRadius: " + calibration.DepthCamera.MetricRadius);
        print("calibration.DepthCamera.Intrinsics.Cx: " + calibration.DepthCamera.Intrinsics.Cx);
        print("calibration.DepthCamera.Intrinsics.Cy: " + calibration.DepthCamera.Intrinsics.Cy);
        print("calibration.DepthCamera.Intrinsics.Fx: " + calibration.DepthCamera.Intrinsics.Fx);
        print("calibration.DepthCamera.Intrinsics.Fy: " + calibration.DepthCamera.Intrinsics.Fy);
        print("calibration.DepthCamera.Intrinsics.K1: " + calibration.DepthCamera.Intrinsics.K1);
        print("calibration.DepthCamera.Intrinsics.K2: " + calibration.DepthCamera.Intrinsics.K2);
        print("calibration.DepthCamera.Intrinsics.K3: " + calibration.DepthCamera.Intrinsics.K3);
        print("calibration.DepthCamera.Intrinsics.K4: " + calibration.DepthCamera.Intrinsics.K4);
        print("calibration.DepthCamera.Intrinsics.K5: " + calibration.DepthCamera.Intrinsics.K5);
        print("calibration.DepthCamera.Intrinsics.K6: " + calibration.DepthCamera.Intrinsics.K6);
        print("calibration.DepthCamera.Intrinsics.Codx: " + calibration.DepthCamera.Intrinsics.Codx);
        print("calibration.DepthCamera.Intrinsics.Cody: " + calibration.DepthCamera.Intrinsics.Cody);
        print("calibration.DepthCamera.Intrinsics.P2: " + calibration.DepthCamera.Intrinsics.P2);
        print("calibration.DepthCamera.Intrinsics.P1: " + calibration.DepthCamera.Intrinsics.P1);

        for (int i = 0; i < 9; ++i)
            print($"rotation[{i}]: {calibration.DepthToColorExtrinsics.Rotation[i]}");

        for (int i = 0; i < 3; ++i)
            print($"translation[{i}]: {calibration.DepthToColorExtrinsics.Translation[i]}");

        meshFilter.mesh = CreateMesh(calibration);
    }

    // Updates _VertexOffsetXVector and _VertexOffsetYVector so the rendered quads can face the headsetCamera.
    // This method gets called right before this ScreenRenderer gets rendered.
    void OnWillRenderObject()
    {
        if (meshRenderer.sharedMaterial == null)
            return;

        // Ignore when this method is called while Unity rendering the Editor's "Scene" (not the "Game" part of the editor).
        if (Camera.current != mainCamera)
            return;

        var cameraTransform = mainCamera.transform;
        var worldCameraFrontVector = cameraTransform.TransformDirection(new Vector3(0.0f, 0.0f, 1.0f));

        // Using the y direction as the up vector instead the up vector of the camera allows the user to feel more
        // comfortable as it preserves the sense of gravity.
        // Getting the right vector directly from the camera transform through zeroing its y-component does not
        // work when the y-component of the camera's up vector is negative. While it is possible to solve the problem
        // with an if statement, inverting when the y-component is negative, I decided to detour this case with
        // usage of the cross product with the front vector.
        var worldUpVector = new Vector3(0.0f, 1.0f, 0.0f);
        var worldRightVector = Vector3.Cross(worldUpVector, worldCameraFrontVector);
        worldRightVector = new Vector3(worldRightVector.x, 0.0f, worldRightVector.z);
        worldRightVector.Normalize();

        var localRightVector = transform.InverseTransformDirection(worldRightVector);
        var localUpVector = transform.InverseTransformDirection(worldUpVector);

        // The coordinate system of Kinect's textures have (1) its origin at its left-up side.
        // Also, the viewpoint of it is sort of (2) the opposite of the viewpoint of the Hololens, considering the typical use case. (this is not the case for Azure Kinect)
        // Due to (1), vertexOffsetYVector = -localCameraUpVector.
        // Due to (2), vertexOffsetXVector = -localCameraRightVector.
        //var vertexOffsetXVector = -localRightVector;
        var vertexOffsetXVector = localRightVector;
        var vertexOffsetYVector = -localUpVector;

        meshRenderer.sharedMaterial.SetVector("_VertexOffsetXVector", new Vector4(vertexOffsetXVector.x, vertexOffsetXVector.y, vertexOffsetXVector.z, 0.0f));
        meshRenderer.sharedMaterial.SetVector("_VertexOffsetYVector", new Vector4(vertexOffsetYVector.x, vertexOffsetYVector.y, vertexOffsetYVector.z, 0.0f));
    }

    private static Mesh CreateMesh(AzureKinectCalibration calibration)
    {
        int width = calibration.DepthCamera.Width;
        int height = calibration.DepthCamera.Height;

        var depthCamera = calibration.DepthCamera;

        var vertices = new Vector3[width * height];
        var uv = new Vector2[width * height];

        for (int i = 0; i < width; ++i)
        {
            for(int j = 0; j < height; ++j)
            {
                float[] xy = new float[2];
                int valid = 0;
                if(AzureKinectIntrinsicTransformation.Unproject(depthCamera, new float[2] { i, j }, ref xy, ref valid))
                {
                    vertices[i + j * width] = new Vector3(xy[0], xy[1], 1.0f);
                }
                else
                {
                    vertices[i + j * width] = new Vector3(0.0f, 0.0f, 0.0f);
                }
                uv[i + j * width] = new Vector2(i / (float)(width - 1), j / (float)(height - 1));
            }
        }

        // Converting the point cloud version of vertices and uv into a quad version one.
        int quadWidth = width - 2;
        int quadHeight = height - 2;
        var quadPositions = new Vector3[quadWidth * quadHeight];
        var quadUv = new Vector2[quadWidth * quadHeight];
        var quadPositionSizes = new Vector2[quadWidth * quadHeight];

        for (int ii = 0; ii < quadWidth; ++ii)
        {
            for (int jj = 0; jj < quadHeight - 2; ++jj)
            {
                int i = ii + 1;
                int j = jj + 1;
                quadPositions[ii + jj * quadWidth] = vertices[i + j * width];
                quadUv[ii + jj * quadWidth] = uv[i + j * width];
                quadPositionSizes[ii + jj * quadWidth] = (vertices[(i + 1) + (j + 1) * width] - vertices[(i - 1) + (j - 1) * width]) * 0.5f;
            }
        }

        var triangles = new int[quadPositions.Length];
        for (int i = 0; i < triangles.Length; ++i)
            triangles[i] = i;
        
        // Without the bounds, Unity decides whether to render this mesh or not based on the vertices calculated here.
        // This causes Unity not rendering the mesh transformed by the depth texture even when the transformed one
        // belongs to the viewport of the camera.
        var bounds = new Bounds(Vector3.zero, Vector3.one * 1000.0f);

        var mesh = new Mesh()
        {
            indexFormat = IndexFormat.UInt32,
            vertices = quadPositions,
            uv = quadUv,
            uv2 = quadPositionSizes,
            bounds = bounds,
        };
        mesh.SetIndices(triangles, MeshTopology.Points, 0);

        return mesh;
    }
}