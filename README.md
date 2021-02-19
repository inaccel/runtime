# InAccel Runtime for Xilinx FINN MP Accelerators
This repository contains a custom runtime imlpementation for the Xilinx [FINN](https://github.com/Xilinx/finn) MP accelerators that couples together two Alveo U280 boards into a single resource.

To build the custom runtime just **clone** the repository and use the **Makefile** provided to generate the output shared library.

``` bash
git clone https://github.com/inaccel/runtime.git -b Xilinx-MP
cd runtime && make libXilinx-MP.so CPPFLAGS="-DXilinx -Iinclude -Iinclude/xrt -IOpenCL-Headers"
```

## InAccel vended FINN accelerators

All of the FINN accelerators (MP and default ones) can be found at [InAccel's online bitstream repository](https://store.inaccel.com/artifactory/webapp/#/artifacts/browse/tree/General/bitstreams/xilinx), packaged using the [InAccel CLI tools](https://docs.inaccel.com/reference/inaccel/cli/) for instant deployment and usage with [InAccel Coral](https://inaccel.com/fpga-manager/). The links for each specific FINN accelerator are listed in the following tables:

### ResNet50

| U250 | U280 | U280 - MP Cases |
| :----: | :----: | :--------------: |
|[ResNet50](https://store.inaccel.com/artifactory/bitstreams/xilinx/u250/xdma_201830.2/xilinx/com/researchlabs/1.2/1resnet50) (1 instance)|[ResNet50](https://store.inaccel.com/artifactory/bitstreams/xilinx/u280/xdma_201920.3/xilinx/com/researchlabs/1.0/1resnet50) (1 instance)|[ResNet TMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.0/1resnet50) (1 instance)|
|||[ResNet HwMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.1/1resnet50) (1 instance)|
|||[ResNet SwMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.2/1resnet50-one_1resnet50-two) (1 instance)|

### MobileNet

| U250 | U280 | U280 - MP Cases |
| :----: | :----: | :--------------: |
|[MobileNet](https://store.inaccel.com/artifactory/bitstreams/xilinx/u250/xdma_201830.2/xilinx/com/researchlabs/1.0/2mobilenet) (2 instances)|[MobileNet](https://store.inaccel.com/artifactory/bitstreams/xilinx/u280/xdma_201920.3/xilinx/com/researchlabs/1.0/1mobilenet) (1 instance)|[MobileNet TMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.0/3mobilenet) (3 instances)|
||[MobileNet](https://store.inaccel.com/artifactory/bitstreams/xilinx/u280/xdma_201920.3/xilinx/com/researchlabs/1.1/2mobilenet) (2 instances)|[MobileNet HwMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.1/3mobilenet) (3 instances)|
|||[MobileNet SwMP](https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.2/2mobilenet_1mobilenet-one_1mobilenet-two) (3 instances)|

To install an accelerator use the `inaccel install <artifact>` command and the corresponding link from the table above. E.g.:
```bash
inaccel install https://store.inaccel.com/artifactory/bitstreams/xilinx/u250/xdma_201830.2/xilinx/com/researchlabs/1.2/1resnet50/
```

## Setup InAccel and Use the Custom Runtime
To setup InAccel and use the custom runtime for the MP accelerators, please follow the steps below.

### Install InAccel and configure License
First of all make sure you have installed Docker. Then, to acquire a **free** License head to [InAccel website](https://inaccel.com/license/). To complete with the installation execute the following code snippet, adding your license to the last command:

```bash
curl -sS https://setup.inaccel.com/repo | sh -s install
systemctl enable docker
systemctl restart docker
inaccel config license <your-license>
```

### Install InAccel-Keras
To install inaccel-keras python package make sure you have already setup Python3 and execute the following:
```bash
pip3 install inaccel-keras
```
### Use the custom runtime (MP cases)
**Build** the custom runtime, move the generated shared library file under `/etc/inaccel/runtimes` and finally edit `etc/inaccel/coral.json` file, with an editor of your choice, so that **libXilinx-MP.so** is used rather than the default libXilinx.so .

``` bash
git clone https://github.com/inaccel/runtime.git -b Xilinx-MP
cd runtime && make libXilinx-MP.so CPPFLAGS="-DXilinx -Iinclude -Iinclude/xrt -IOpenCL-Headers"
sudo mv libXilinx-MP.so /etc/inaccel/runtimes
sudo nano /etc/inaccel/coral.json #change libXilinx.so to libXilinx-MP.so
```

### Start InAccel
Use the 2.0.0 tag of inaccel/coral image when starting inaccel.

```bash
inaccel start -t 2.0.0
```

You're all set up! You can now develop your own application or use one of the following simple examples:

## Download Sample Images
```bash
wget https://github.com/inaccel/keras/raw/master/examples/data/dog.jpg
wget https://github.com/inaccel/keras/raw/master/examples/data/elephant.jpg
```

### ResNet50 Example
1. Install one of the ResNet50 MP bitstreams available. E.g.:
	```bash
	inaccel install https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.0/1resnet50/
	```
2. Fetch the ResNet50 model:
	```bash
	wget https://inaccel-demo.s3.amazonaws.com/models/resnet50_weights.h5
	```

3. Run the application

	```python
	import numpy as np

	from inaccel.keras.applications.resnet50 import decode_predictions, ResNet50
	from inaccel.keras.preprocessing.image import load_img

	model = ResNet50(weights='resnet50_weights.h5')

	dog = load_img('dog.jpg', target_size=(224, 224))
	dog = np.expand_dims(dog, axis=0)

	elephant = load_img('elephant.jpg', target_size=(224, 224))
	elephant = np.expand_dims(elephant, axis=0)

	images = np.vstack([dog, elephant])

	preds = model.predict(images)

	print('Predictions:', decode_predictions(preds, top=5))
	```

### MobileNet Example

1. Install one of the MobileNet MP bitstreams available. E.g.:
	```bash
	inaccel install https://store.inaccel.com/artifactory/bitstreams/xilinx/2xu280/xdma_201920.3/xilinx/com/researchlabs/1.0/3mobilenet/
	```
2. Run the application

	```python
	import numpy as np

	from inaccel.keras.applications.mobilenet import decode_predictions, MobileNet
	from inaccel.keras.preprocessing.image import load_img

	model = MobileNet()

	dog = load_img('dog.jpg', target_size=(224, 224))
	dog = np.expand_dims(dog, axis=0)

	elephant = load_img('elephant.jpg', target_size=(224, 224))
	elephant = np.expand_dims(elephant, axis=0)

	images = np.vstack([dog, elephant])

	preds = model.predict(images)

	print('Predictions:', decode_predictions(preds, top=5))
	```

### MLPerf benchmark and Patching
To run the MLPerf benchmark you have to first find and download the imagenet dataset. Then do the following:

1. Fetch the ResNet50 model and setup environment:
	```bash
	wget https://inaccel-demo.s3.amazonaws.com/models/resnet50_weights.h5

	#set the model path (folder containing resnet50_weights.h5 file)
	export MODEL_DIR=$(pwd)
	#set the data path (imagenet folder containing the images)
	export DATA_DIR=/path/to/imagenet
	```
2. Clone MLPerf repo and perform InAccel's patch:
	```bash
	# Clone MLPerf repo
	git clone https://github.com/mlperf/inference_results_v0.7.git mlperf
	# Copy the updated patch under mlperf/open/InAccel/code/resnet/inaccel-keras folder
	cp mlperf.patch mlperf/open/InAccel/code/resnet/inaccel-keras

	cd mlperf/open/InAccel/code/resnet/inaccel-keras
	git submodule add https://github.com/mlcommons/inference

	# Buil and install benchmark
	make all
	```
	:warning: Make sure **python** command points to **Python3** installation or else you must edit the `Makefile` as well as the `run_local.sh` script under `inference/vision/classification_and_detection/` and change python to python3.
3. Run ResNet50 or MobileNet:
	```bash
	cd inference/vision/classification_and_detection/

	# ResNet50
	./run_local.sh inaccel resnet50 --max-batchsize 400 --qps 10000 --scenario Offline
	# MobileNet
	./run_local.sh inaccel mobilenet --max-batchsize 400 --qps 10000 --scenario Offline
	```
