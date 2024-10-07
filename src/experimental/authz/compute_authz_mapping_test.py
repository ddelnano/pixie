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

    row = df.count().head()
    single_row_df = spark_fixture.createDataFrame([row])

    expected = [{
        "client_name": "",
        "service_name": "kube-dns",
        "http_target": "/readiness",
        "http_method": "GET",
        "count": 29,
    }]
    expected_df = spark_fixture.createDataFrame(expected).select("service_name", "client_name", "http_target", "http_method", "count")
    assertDataFrameEqual(single_row_df, expected_df)
