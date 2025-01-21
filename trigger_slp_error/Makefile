DRIVER	?= hyperkit
CPUS		?= 4
MEMORY	?= 16384
DISKSZ	?= "100GB"

SRCS = $(wildcard src/*)
KUBE = $(wildcard kube/*)
BASH = $(wildcard bash/*)

# start-minikube:
# 	ec
# 	minikube start --driver=${DRIVER} --cpus=4 --memory=${MEMORY} --disk-size=${DISKSZ}
# 	touch $@

image: Dockerfile ${SRCS} ${BASH}
	docker build -t ddelnano/mock-go-server:latest .
	docker push ddelnano/mock-go-server:latest
	touch $@

deploy-pixie:
	# px deploy -y
	# touch $@
	echo "Pixie is already deployed";

short-example: kube/short-request-manifest.yaml deploy-pixie image
	kubectl apply -f $<

long-example: kube/long-request-manifest.yaml deploy-pixie image
	kubectl apply -f $<

clean:
	minikube delete
	rm image
	rm deploy-pixie
