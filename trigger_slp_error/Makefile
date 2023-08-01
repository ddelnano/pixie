DRIVER	?= hyperkit
CPUS		?= 4
MEMORY	?= 16384
DISKSZ	?= "100GB"

SRCS = $(wildcard src/*)
KUBE = $(wildcard kube/*)
BASH = $(wildcard bash/*)

start-minikube:
	minikube start --driver=${DRIVER} --cpus=4 --memory=${MEMORY} --disk-size=${DISKSZ}
	touch $@

image: start-minikube Dockerfile ${SRCS} ${BASH}
	eval $$(minikube docker-env); docker build -t mock-go-server:latest .
	touch $@

deploy-pixie: start-minikube
	px deploy -y
	touch $@

short-example: kube/short-request-manifest.yaml deploy-pixie image
	kubectl apply -f $<

long-example: kube/long-request-manifest.yaml deploy-pixie image
	kubectl apply -f $<

clean:
	minikube delete
	rm start-minikube
	rm image
	rm deploy-pixie
