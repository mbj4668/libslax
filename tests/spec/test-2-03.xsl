<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="html" encoding="utf-8"/>
  <xsl:template match="measurement">
    <xsl:variable name="m">
      <xsl:choose>
        <xsl:when test="@fromunit = &quot;km&quot;">
          <xsl:value-of select=". * 1000"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;m&quot;">
          <xsl:value-of select="."/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;cm&quot;">
          <xsl:value-of select=". * 0.01"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;mm&quot;">
          <xsl:value-of select=". * 0.001"/>
        </xsl:when>
      </xsl:choose>
    </xsl:variable>
    <measurement unit="{@tounit}">
      <xsl:choose>
        <xsl:when test="@tounit = &quot;mi&quot;">
          <xsl:value-of select="0.00062137 * $m"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;yd&quot;">
          <xsl:value-of select="1.09361 * $m"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;ft&quot;">
          <xsl:value-of select="3.2808 * $m"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;in&quot;">
          <xsl:value-of select="39.37 * $m"/>
        </xsl:when>
      </xsl:choose>
    </measurement>
  </xsl:template>
</xsl:stylesheet>
