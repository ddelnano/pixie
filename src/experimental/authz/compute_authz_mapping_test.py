import pytest

from pyspark.testing.utils import assertDataFrameEqual

from compute_authz_mapping import compute_authz_service_mapping

@pytest.fixture
def spark_fixture():
    from pyspark.sql import SparkSession
    spark = SparkSession.builder.appName("etl_test").getOrCreate()
    yield spark
    spark.stop()

def test_authz_service_mapping(spark_fixture):
    # load data from the otel-export-authz-source.json file
    data_source = spark_fixture.read.json("data/otel-export-authz-source.json")
    df = compute_authz_service_mapping(data_source)

    # The test file contains a ton of data, but we only care to match the rough shape of the output
    row = df.count().head()
    single_row_df = spark_fixture.createDataFrame([row])

    expected = [{
        "client_name": "vizier-query-broker",
        "service_name": "vizier-metadata-svc",
        "http_target": "/px.vizier.services.metadata.CronScriptStoreService/RecordExecutionResult",
        "http_method": "POST",
        "count": 15,
    }]
    # The .select forces the columns to be in the same order as df
    expected_df = spark_fixture.createDataFrame(expected).select("service_name", "client_name", "http_target", "http_method", "count")
    assertDataFrameEqual(single_row_df, expected_df)
