package main

import (
	"context"
	"fmt"
	"io"
	"os"

	"px.dev/pixie/src/api/go/pxapi"
	"px.dev/pixie/src/api/go/pxapi/errdefs"
	"px.dev/pixie/src/api/go/pxapi/types"
)

// This script was taken from the pxl scripts bundled within the newrelic pixie plugin and is defined here:
// https://github.com/pixie-io/pixie-plugin/blob/a4c442cf62071473b70cab143ffa46202d1ea8d1/plugins/new-relic/retention.yaml#L103
var (
	pxl = `
import px

def remove_ns_prefix(column):
    return px.replace('[a-z0-9\-]*/', column, '')

def add_source_dest_columns(df):
    df.pod = df.ctx['pod']
    df.node = df.ctx['node']
    df.container = df.ctx['container']
    df.deployment = df.ctx['deployment']

    # If remote_addr is a pod, get its name. If not, use IP address.
    df.ra_pod = px.pod_id_to_pod_name(px.ip_to_pod_id(df.remote_addr))
    df.ra_name = px.select(df.ra_pod != '', df.ra_pod, px.nslookup(df.remote_addr))
    df.ra_node = px.pod_id_to_node_name(px.ip_to_pod_id(df.remote_addr))
    df.ra_deployment = px.pod_id_to_deployment_name(px.ip_to_pod_id(df.remote_addr))

    df.is_server_tracing = df.trace_role == 2
    # Set client and server based on trace_role.
    df.source_pod = px.select(df.is_server_tracing, df.ra_name, df.pod)
    df.destination_pod = px.select(df.is_server_tracing, df.pod, df.ra_name)
    df.destination_node = px.select(df.is_server_tracing, df.node, df.ra_node)
    df.destination_deployment = px.select(df.is_server_tracing, df.deployment, df.ra_deployment)

    df.source_service = px.pod_name_to_service_name(df.source_pod)
    df.destination_service = px.pod_name_to_service_name(df.destination_pod)

    df.destination_namespace = px.pod_name_to_namespace(df.destination_pod)
    df.source_namespace = px.pod_name_to_namespace(df.source_pod)
    df.source_node = px.pod_id_to_node_name(px.pod_name_to_pod_id(df.source_pod))
    df.source_deployment = px.pod_name_to_deployment_name(df.source_pod)
    df.source_deployment = remove_ns_prefix(df.source_deployment)
    df.source_service = remove_ns_prefix(df.source_service)
    df.destination_service = remove_ns_prefix(df.destination_service)
    df.source_container = px.select(df.is_server_tracing, '', df.container)
    df.source_pod = remove_ns_prefix(df.source_pod)
    df.destination_pod = remove_ns_prefix(df.destination_pod)
    return df

df = px.DataFrame('http_events', start_time=px.plugin.start_time, end_time=px.plugin.end_time)
# df = px.DataFrame('http_events')
df.start_time = px.plugin.start_time
df.end_time = px.plugin.end_time
df = add_source_dest_columns(df)

df = df.head(15000)
df.start_time = df.time_ - df.latency
df.host = px.pluck(df.req_headers, 'Host')
df.req_url = df.host + df.req_path
df.user_agent = px.pluck(df.req_headers, 'User-Agent')
df.trace_id = px.pluck(df.req_headers, 'X-B3-TraceId')
df.span_id = px.pluck(df.req_headers, 'X-B3-SpanId')
df.parent_span_id = px.pluck(df.req_headers, 'X-B3-ParentSpanId')

# Strip out all but the actual path value from req_path
df.req_path = px.uri_recompose('', '', '', 0, px.pluck(px.uri_parse(df.req_path), 'path'), '', '')

# Replace any Hex IDS from the path with <id>
df.req_path = px.replace('/[a-fA-F0-9\-:]{6,}(/?)', df.req_path, '/<id>\\1')

df.cluster_name = px.vizier_name()
df.cluster_id = px.vizier_id()
df.pixie = 'pixie'

#px.display(df)
px.export(
  df, px.otel.Data(
    resource={
      # While other Pixie entities use service.name=source_service,
      # the Services-OpenTelemetry entity is set up to only show clients so we use service.name=destination_service.
      'service.name': df.destination_service,
      'service.instance.id': df.destination_pod,
      'k8s.pod.name': df.destination_pod,
      'k8s.deployment.name': df.destination_deployment,
      'k8s.namespace.name': df.destination_namespace,
      'k8s.node.name': df.destination_node,
      # 'px.cluster.id': df.cluster_id,
      # 'k8s.cluster.name': df.cluster_name,
      'instrumentation.provider': df.pixie,
    },
    data=[
      px.otel.trace.Span(
        name=df.req_path,
        start_time=df.start_time,
        end_time=df.time_,
        trace_id=df.trace_id,
        span_id=df.span_id,
        parent_span_id=df.parent_span_id,
        kind=px.otel.trace.SPAN_KIND_SERVER,
        attributes={
          # NOTE: the integration handles splitting of services.
          'parent.namespace.name': df.source_namespace,
          'parent.service.name': df.source_service,
          'parent.deployment.name': df.source_deployment,
          'parent.node.name': df.source_node,
          'parent.k8s.pod.name': df.source_pod,
          'http.method': df.req_method,
          'http.url': df.req_url,
          'http.target': df.req_path,
          'http.host': df.host,
          'http.status_code': df.resp_status,
          'http.user_agent': df.user_agent,
          'http.req_headers': df.req_headers,
          'http.resp_headers': df.resp_headers,
        },
      ),
    ],
  ),
)
`
)

func main() {
	hostname, err := os.Hostname()
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	// Create a Pixie client with local standalonePEM listening address
	ctx := context.Background()
	client, err := pxapi.NewClient(ctx, pxapi.WithDirectAddr("0.0.0.0:12345"))
	if err != nil {
		panic(err)
	}
	// Create a connection to the host.
	vz, err := client.NewVizierClient(ctx, hostname)
	if err != nil {
		panic(err)
	}
	// Create TableMuxer to accept results table.
	tm := &tableMux{}
	// Execute the PxL script.
	resultSet, err := vz.ExecuteScript(ctx, pxl, tm)
	if err != nil && err != io.EOF {
		panic(err)
	}
	// Receive the PxL script results.
	defer resultSet.Close()
	if err := resultSet.Stream(); err != nil {
		if errdefs.IsCompilationError(err) {
			fmt.Printf("Got compiler error: \n %s\n", err.Error())
		} else {
			fmt.Printf("Got error : %+v, while streaming\n", err)
		}
	}
	// Get the execution stats for the script execution.
	stats := resultSet.Stats()
	fmt.Printf("Execution Time: %v\n", stats.ExecutionTime)
	fmt.Printf("Bytes received: %v\n", stats.TotalBytes)
}

// Satisfies the TableRecordHandler interface.
type tablePrinter struct{}

func (t *tablePrinter) HandleInit(ctx context.Context, metadata types.TableMetadata) error {
	return nil
}
func (t *tablePrinter) HandleRecord(ctx context.Context, r *types.Record) error {
	for _, d := range r.Data {
		fmt.Printf("%s ", d.String())
	}
	fmt.Printf("\n")
	return nil
}

func (t *tablePrinter) HandleDone(ctx context.Context) error {
	return nil
}

// Satisfies the TableMuxer interface.
type tableMux struct {
}

func (s *tableMux) AcceptTable(ctx context.Context, metadata types.TableMetadata) (pxapi.TableRecordHandler, error) {
	return &tablePrinter{}, nil
}
